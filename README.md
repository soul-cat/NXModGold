# NXModGold

在已破解的 Nintendo Switch 上为《空洞骑士：丝之歌》实现 PC 端 mod 才有的 HUD 功能。

| | |
|---|---|
| 目标游戏 | Hollow Knight: Silksong |
| Title ID | `010013C00E930000` |
| 目标平台 | Switch 1（Erista / Mariko）+ Atmosphère |
| 宿主开发环境 | macOS (Intel x86_64) |

## 为什么需要这个项目

PC 端已有成熟的血条 mod（`ShowDamage HealthBar`）和收集物标注 mod（`Completionist Map`），
全部基于 BepInEx + Harmony。**这些在 Switch 上一个都用不了**：

| | PC 版 | Switch 版 |
|---|---|---|
| Unity 脚本后端 | **Mono** | **IL2CPP** |
| 托管程序集 | `Assembly-CSharp.dll` 存在 | 无 —— C# 已 AOT 编译成 ARM64 |
| 运行时 patch | BepInEx + Harmony 可用 | 无从下手 |

Atmosphère 的金手指 VM 也做不到：它只能读写内存，**没有任何绘图指令**。
而血条和地图标注的本质是「创建并渲染新的 UI」。

所以只剩一条路：**原生注入 + ARM64 内联 hook**。

## 架构

```
┌─ 游戏进程 (Silksong, main NSO)
│    [plugin/]  SaltyNX 插件，ARM64 原生代码
│      ├─ 内联 hook HealthManager::TakeDamage
│      ├─ 读取 hp / initHp / DamageDealt
│      └─ seqlock 写入 SharedMemory（无锁，绝不阻塞游戏主线程）
│                    │
│                    ▼  SaltyNX SharedMemory
└─ [overlay/]  Tesla .ovl，常驻层 + 输入穿透
       └─ 读 SharedMemory → libtesla 绘制血条与伤害数字
```

### SaltyNX 的三个硬约束

| 约束 | 出处 | 我们的应对 |
|---|---|---|
| 不提供内联 hook | 全仓库 grep "hook" 零命中 | 自己写指令重定位器（`arm64_reloc.hpp`）|
| 拿不到 RWX | 内核只给 R/RW/RX；`svcCreateCodeMemory` 零售 NPDM 里没有 | trampoline 池放在插件自己的 `.text`，用 `SaltySD_Memcpy` 绕页权限写入 |
| `SaltySD_Memcpy` 只在 `main()` 期间可用 | Core 在 `LoadPlugins()`(`main.c:466`) 后立刻 `SaltySD_Deinit()`(`:496`) | 所有 hook 在入口一次装完 |

⚠️ `SaltySDCore_getCodeStart()` 返回的是 **rtld，不是 main**。拿 main 基址只能用
NX-FPS 的启发式走法（`getCodeStart() + 0x4000` 后 `svcQueryMemory` 扫最多 3 个区域）。
不是 API 契约，但整个零售游戏库都这么用。

### 时序：`main()` 跑在游戏执行任何自身代码之前

```
Core main():
  :466  SaltySDCore_LoadPlugins()   ← 插件 main() 在此
  :496  SaltySD_Deinit()
  ...   __libnx_exit(0) → rtld → 游戏启动
```

此时 **IL2CPP 尚未初始化**。对我们无害 —— 我们只写指令字节，NSO 已映射且
`.text` 内容已最终确定（重定位只动 `.data`/`.got`）；hook 在游戏后来调用
`TakeDamage` 时才触发，且 hook 体只按固定偏移读字段，不碰 IL2CPP 运行时。

### 三个关键设计决策

**1. 归一化屏幕坐标 `[0,1]`**
插件不感知 overlay 分辨率，overlay 不感知游戏分辨率。掌机/底座切换、
第三方分辨率 mod 都不会破坏对齐。

已验证：libtesla 的 framebuffer 像素与 1280×720 游戏屏幕空间 **1:1 对应**，
所以 `screenX * 1280` 可以直接喂给 `drawRect`。

**2. seqlock 而非互斥锁**
hook 运行在游戏主线程上，绝不能因为 overlay 读数据而阻塞。
写者在 `publish()` 前后各递增一次序号，读者看到奇数或前后不一致就重读。

**3. 不使用 `dmnt:cht`**
纯读内存方案要走 `dmnt:cht`，而它有 **2 个并发会话的硬上限**
（`dmnt_main.cpp: CheatMaxSessions = 2`），会和 EdiZon 抢。
我们让插件直接写共享内存，全程不碰这个服务。

## 目录

| 路径 | 用途 |
|------|------|
| `shared/hud_protocol.h` | 插件 ↔ overlay 共享内存协议。**两边必须同步改** |
| `plugin/source/tracker.hpp` | 敌人状态追踪器。定长 24 槽，无分配，LRU + TTL 双重淘汰 |
| `plugin/source/arm64_reloc.hpp` | ARM64 指令重定位。宁可拒绝也不生成半对的代码 |
| `plugin/source/hook_pool.hpp` | hook 规划（纯函数，可宿主机测试）+ trampoline 池布局 |
| `plugin/source/main.cpp` | 插件入口：模块基址、Build ID 校验、共享内存、hook 安装 |
| `overlay/source/hud_reader.hpp` | seqlock 读取端 + 坐标换算 |
| `tools/` | macOS 侧分析流水线 |
| `overlay/source/hud_layout.hpp` | 血条布局与配色（纯函数，可宿主机测试）|
| `overlay/source/main.cpp` | Tesla overlay：常驻层、共享内存接入、绘制 |
| `tests/` | 宿主机单元测试（不需要 devkitPro）|
| `HANDOFF.md` | **接手文档** —— 坑、不变量、风险、推进顺序 |
| `docs/` | 研究结论与决策记录 |

## 快速开始

### 1. 环境准备（一次性）

```bash
./tools/00-setup.sh
```

会检查 devkitPro 与 .NET，并把 Il2CppDumper 拉到 `vendor/`。
devkitPro 需要 sudo，脚本会打印命令让你手动执行。

### 2. 跑测试（不需要游戏转储，随时可跑）

```bash
make -C tests test     # 4 个测试二进制，ASan + UBSan
make -C tests tsan     # ThreadSanitizer（单独一轮）
./tools/nsotool.py selftest
```

| 测试 | 覆盖 |
|---|---|
| `test_tracker` | 槽位管理、TTL/LRU 淘汰、maxHp 推断、边界防护 |
| `test_seqlock` | 真线程并发，按写者不变量逐条校验快照一致性 |
| `test_reloc` | 指令重定位。参考编码来自系统汇编器，再做性质测试 |
| `test_hookplan` | hook 可安装性判定与拒绝路径 |
| `test_layout` | 血条布局、淡出、配色、越界防护、共享内存扫描 |

### 3. 转储游戏（需要主机在手边）

用 **nxdumptool**，不需要在 Mac 上放 `prod.keys`：

1. 主机启动 nxdumptool，选 Hollow Knight: Silksong
2. 进 `NCA / Ticket / RomFS / ExeFS options`
3. 导出 **ExeFS section data** → 得到 `main`（所有 RVA 的来源）
4. 导出 **RomFS section data** → 得到 `global-metadata.dat`
   （路径可能是 `Data/Managed/Metadata/` 或 `Data/il2cpp_data/Metadata/`，
   脚本会自动 `find`）
5. 把 SD 卡上 `switch/nxdumptool/` 的结果拷到 Mac

### 4. 生成偏移

```bash
./tools/10-extract.sh <转储目录>   # 定位文件、打印 Build ID 与段布局
./tools/20-dump-il2cpp.sh          # 跑 Il2CppDumper
./tools/30-gen-offsets.py          # dump.cs -> plugin/source/offsets.h
```

`offsets.h` 是生成产物，**不入库**。游戏每次更新后重新生成即可。

### 5. 编译

```bash
make -C plugin       # 产物是裸 .elf，不是 .nro/.nso
make -C overlay      # 产物是 .ovl
```

overlay 需要**改造版 libtesla** —— 上游 `WerWolv/libtesla` framebuffer 尺寸
写死 448x720、没有 `setLayerPos()`、无条件画黑底和页脚，做不出常驻 HUD。
**实际采用**的是 Status-Monitor-Overlay vendored 的那份 fork（`TeslaFPS`/
`FullMode`/`alphabackground` 常驻配方 + `setLayerPos` 都在，无 curl 依赖）：

```bash
git clone --depth 1 --filter=blob:none --sparse \
    https://github.com/masagrator/Status-Monitor-Overlay.git vendor/status-monitor
git -C vendor/status-monitor sparse-checkout set lib/libtesla include
```

注意（2026-09 实测）：`ppkantorski/libultrahand` 仓库的 libtesla 是另一套
大改版 API——常驻全局变量藏在 `IS_STATUS_MONITOR_DIRECTIVE` 条件编译里，
还引入 curl/zlib/minizip 依赖（minizip 无 dkp 包）——**别用**。
另外这份 vendored tesla 是"头文件即实现"：整个项目必须恰好在一个 .cpp 里
`#define TESLA_INIT_IMPL` 后 include tesla.hpp（见 overlay/source/main.cpp）。

devkitPro 原生 macOS 包的可用性未确认（官网被 Cloudflare 挡；Intel x86_64
历史上受支持，但维护者已不常测 Intel mac，现状未知，装上 dkp-pacman 试一次即知）。
确定可用的路径是 Docker：`devkitpro/devkita64` 的 `linux/amd64` 与
`linux/arm64` manifest 均已验证（2026-09，Docker Hub API），Intel Mac 上容器原生运行、
无仿真开销：

```bash
docker run --rm -v "$PWD:/work" -w /work devkitpro/devkita64:latest make -C plugin
```

### 6. 安装

```
SD:/SaltySD/plugins/010013C00E930000/sshud.elf      ← 插件
SD:/switch/.overlays/sshud-overlay.ovl              ← overlay
```

注意大小写：`plugins/<TITLEID>/` 用**大写**十六进制（`%016lX`），
而 `patches/<titleid>/` 用小写。别弄混。

需要 SaltyNX **≥ 1.7.3** —— 1.5.0 才修好 Unity 6 兼容性（更新日志点名
Hollow Knight Silksong），1.7.3 正式修了 Unity 6 依赖的 `GetAllModuleInfo`。

## 工具说明

### `tools/nsotool.py`

纯 Python 的 NSO0 解析器，无第三方依赖。

```bash
./tools/nsotool.py info       work/main.nso   # Build ID + 段布局
./tools/nsotool.py buildid    work/main.nso   # 只打 Build ID
./tools/nsotool.py decompress work/main.nso -o work/main.bin
./tools/nsotool.py selftest                   # LZ4 解压器自检
```

内置 LZ4 块解压器与 NSO 头部解析，附 17 个手工构造的测试向量
（含重叠匹配、255 续位长度编码、.bss 零填充、4 条错误路径）。
会检测 NSO flags bit 7（`UseZbicCompression`，zstd 而非 LZ4）并明确报错，
而不是静默解出垃圾数据。

### `tools/30-gen-offsets.py`

从 `dump.cs` 提取符号。要抓什么在 `tools/targets.json` 里配置，
支持字段别名、命名空间消歧、重载检测，并用 `--image-size` 校验
RVA 落在合法范围内。

必需符号缺失时以 exit code 2 失败并打印自查命令 —— 包括
Il2CppDumper 报 `RVA: -1`（方法无代码）的情况。

## 技术笔记

### 地址换算（最容易搞错的一点）

```
运行时地址 = 模块基址 + RVA          ← 不减任何东西
```

Il2CppDumper 对 NSO 报的 RVA 锚定在**模块加载基址**，不是 `.text` 的 `mem_off`。

依据：`Il2Cpp.GetRVA` 默认是恒等函数（`Il2Cpp.cs:343`），PE 覆写成 `pointer - ImageBase`、
Macho64 覆写成 `pointer - vmaddr`，而 **`NSO.cs` 根本没覆写**。所以对 NSO 而言
`RVA == VA == 二进制里存的原始指针`，这也是 dump.cs 里这三个数字相同的原因。

NSO 的 `mem_off` 字段本身就定义了一个以模块基址为原点、横跨
`.text → .rodata → .data → .bss` 的连续地址空间，RVA 直接活在这个空间里。
**`.text mem_off` 只用于判断段归属，绝不参与地址运算**（减掉会造成二次修正）。

换算表：

| 目标 | 公式 |
|---|---|
| 运行时地址 | `模块基址 + RVA` |
| Atmosphère 金手指偏移（region M=0） | `RVA`（同一坐标系，无需换算） |
| Ghidra（Adubbz Switch Loader） | `0x7100000000 + RVA` |
| Il2CppInspectorRedux 报的地址 | `0x7100000000 + RVA`（要减回去） |
| dump.cs 的 `Offset:` 列 | **解压后** NSO 的文件偏移，不是磁盘文件偏移 |

合法 RVA 范围是 `[0, image_size)`，其中 `image_size` 含 `.bss`
（`nsotool.py info` 会打印）。`30-gen-offsets.py --image-size` 会据此校验。

### 工具链要点

- Silksong 是 **Unity 6000.0.50f1** → metadata **v31**，
  上游 `Perfare/Il2CppDumper` v6.7.46 支持 v16–v31，**不需要第三方分支**
  （v35–v39 的分支是给 Unity 6000.3+ 的）
- Il2CppDumper **内部会解压 LZ4 NSO**，不需要先跑 hactool
- 运行时必须设 `DOTNET_ROLL_FORWARD=LatestMajor`（预编译包 target net6/net7）
- **输出目录必须先存在**，否则被静默忽略、产物落到 dll 旁边
- `config.json` 必须和 dll 同目录，且 `RequireAnyKey` 要设成 `false`，
  否则非交互运行会卡在 `Console.ReadKey` 上不退出

### 目标类结构

来源：Silksong PC 版（Mono 后端）的公开反编译。Switch 版类结构相同，只有地址要自己 dump。

```csharp
public class HealthManager : MonoBehaviour, IHitResponder, ITagDamageTakerOwner, IInitialisable
{
    public  int hp;        // 当前血量
    private int initHp;    // 最大血量 —— 没有 maxHP/MaxHP 这个名字
    public  bool isDead;
    private bool invincible;
    private HealthManager sendDamageTo;

    public  IHitResponder.HitResponse Hit(HitInstance hitInstance);   // 公开入口
    private HitInstance ApplyDamageScaling(HitInstance hitInstance);
    private void TakeDamage(HitInstance hitInstance);                 // 单一重载
}

[Serializable]
public struct HitInstance        // 是 struct，不是 class
{
    public int  DamageDealt;
    public bool IsHeroDamage;    // 过滤玩家造成的伤害
    public float Multiplier;
}
```

**最大血量是 `initHp`。** 在 init 时由 `initHp = hp` 捕获一次，复活时 `hp = initHp` 还原。
有意思的是 PC 版血条 mod 压根没读它 —— 它用 `Mathf.Max(打前, 打后)` 推断。
我们两条路都支持：读到 `initHp` 就用真值，读不到退化为推断。

**hook 点选择**：主 hook 是 `TakeDamage`，它拿到的是 `ApplyDamageScaling` 之后的最终伤害。
若 dump 里 `TakeDamage` 报 `RVA: -1`，改 hook `Hit` —— 它是 `IHitResponder` 的接口实现，
天然不会被裁剪，且有固定 vtable slot 可兜底。

⚠️ `HitInstance` 是大号 by-value struct，ARM64 上会**通过隐藏指针传递**（`this` 在 x0，
struct 指针在 x1），不是寄存器传参。写 hook 前先在反汇编里确认。

### Tesla 常驻 overlay

上游 libtesla 做不到常驻 + 输入穿透（framebuffer 尺寸写死、无 `setLayerPos()`、
无条件画黑底和页脚）。必须用 Ultrahand / Status-Monitor 的改造版。

配方（在 Gui 构造函数里）：

```cpp
FullMode = false;                    // 去掉全屏边框
TeslaFPS = <刷新率>;                 // 接管帧节奏
deactivateOriginalFooter = true;     // 去掉 "Back / OK"
alphabackground = 0x0;               // fillScreen 变透明清屏
tsl::hlp::requestForeground(false);  // 把输入还给游戏
```

**永远不要调用 `hide()`。**

⚠️ **输入是复制，不是移交。** overlay 进程是 `AppletType_None`，直接读 HID 共享内存，
所以游戏和 overlay 会同时收到同一次按键。HUD 热键必须避开游戏用到的键。

⚠️ Status-Monitor-Overlay **已停止维护**，继任者是
`masagrator/Status-Monitor-Deux`（基于 `ppkantorski/libultrahand`）。

### 内存预算

nx-ovlloader 的 applet 堆是 6 MB（`g_appletHeapSize = 0x600000`）：

| framebuffer | 双缓冲占用 | 评价 |
|---|---|---|
| 1280×720 全屏 | 3.6 MB | 偏紧 |
| **1280×256 条带** | **1.3 MB** | **v1 采用** |

`framebufferWidth` 必须是 32 的倍数，且只在 `Renderer::init()` 读一次，
必须在 `tsl::loop()` 之前设好。位置 `setLayerPos()` 可以运行时改。

## 当前进度

| # | 步骤 | 状态 |
|---|------|------|
| 1 | 共享内存协议 | ✅ |
| 2 | 敌人追踪器 | ✅ 10 项测试 |
| 3 | seqlock 读取端 | ✅ 8 项测试 + 880 万份并发快照零撕裂 |
| 4 | NSO 解析 / LZ4 解压 | ✅ 17 个测试向量 |
| 5 | 偏移生成器 | ✅ fixture + 2 条失败路径验证 |
| 6 | IL2CPP dump 流水线 | ✅ 脚本就绪，待转储输入 |
| 7 | 游戏转储 | ⏸ 需要主机在手边 |
| 8 | ARM64 指令重定位器 | ✅ 4 万余条性质验证 |
| 9 | hook 规划逻辑 | ✅ 6 项测试 |
| 10 | SaltyNX 插件入口 + hook 安装 | ✅ 已写，**未编译验证**（本机无 devkitPro）|
| 11 | plugin/Makefile | ✅ 已写，**未编译验证** |
| 12 | 血条布局逻辑 | ✅ 11 项测试 |
| 13 | Tesla overlay 渲染 | ✅ 已写，**未编译验证** |
| 14 | overlay/Makefile | ✅ 已写，**未编译验证** |
| 15 | 收集物地图标注 | 📋 v2 |

## 范围说明

**v1 只做屏幕固定位置血条，不做怪物头顶跟随。**

跟随需要每帧拿敌人屏幕坐标，路径是
`HealthManager → get_gameObject → get_transform → get_position`，
而 `Transform.get_position` 在 IL2CPP 里是 icall，正确调用要依赖
`get_position_Injected` 的地址和 Unity 原生对象内存布局 —— 随 Unity 小版本变化，很脆。

PC 端三个血条 mod 里有两个本来就是屏幕固定位置。v1 对齐它们。
协议里已预留 `screenX/screenY` + `SSHUD_FLAG_POS_VALID`，
v2 加世界坐标跟随**不需要改协议**。

## 风险

- **Build ID 绑定**：游戏每次更新所有 RVA 全变。插件启动时校验 Build ID，
  不匹配直接退出，绝不盲目 patch
- **Switch 2 不适用**：目前没有公开可用的 CFW
- **联机封号**：装任何 CFW 插件后不要联网，建议 90DNS 或飞行模式

## 参考

| 项目 | 用途 |
|------|------|
| [Atmosphere-NX/Atmosphere](https://github.com/Atmosphere-NX/Atmosphere) | CFW 本体 |
| [masagrator/SaltyNX](https://github.com/masagrator/SaltyNX) | 原生注入框架 |
| [masagrator/NX-FPS](https://github.com/masagrator/NX-FPS) | 插件参考实现 |
| [ppkantorski/libultrahand](https://github.com/ppkantorski/libultrahand) | 改造版 libtesla |
| [Perfare/Il2CppDumper](https://github.com/Perfare/Il2CppDumper) | IL2CPP 符号还原 |
| [jakcron/nstool](https://github.com/jakcron/nstool) | NSO/NCA/XCI 处理 |
| [jcx515250418qq/Silksong_HealthBar](https://github.com/jcx515250418qq/Silksong_HealthBar) | PC 血条 mod，逻辑参考 |

## 法律边界

逆向与自制软件研究。只对自己的主机、自己的游戏做转储与修改。
不分发游戏数据、密钥或转储产物 —— `.gitignore` 已覆盖相关模式。
