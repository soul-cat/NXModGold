# NXModGold

在已破解的 Nintendo Switch 上为《空洞骑士：丝之歌》实现 PC 端 mod 才有的 HUD 功能。

| | |
|---|---|
| 目标游戏 | Hollow Knight: Silksong |
| Title ID | `010013C00E930000` |
| 目标平台 | Switch 1（Erista / Mariko）+ Atmosphère |
| 宿主开发环境 | macOS (Apple Silicon) |

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
│      ├─ 读取 hp / maxHp / 伤害值
│      └─ seqlock 写入 SharedMemory（无锁，绝不阻塞游戏主线程）
│                    │
│                    ▼  SaltyNX SharedMemory
└─ [overlay/]  Tesla .ovl，常驻层 + 输入穿透
       └─ 读 SharedMemory → libtesla 绘制血条与伤害数字
```

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
| `overlay/source/hud_reader.hpp` | seqlock 读取端 + 坐标换算 |
| `tools/` | macOS 侧分析流水线 |
| `tests/` | 宿主机单元测试（不需要 devkitPro） |

## 快速开始

### 1. 环境准备（一次性）

```bash
./tools/00-setup.sh
```

会检查 devkitPro 与 .NET，并把 Il2CppDumper 拉到 `vendor/`。
devkitPro 需要 sudo，脚本会打印命令让你手动执行。

### 2. 跑测试（不需要游戏转储，随时可跑）

```bash
make -C tests test     # ASan + UBSan
make -C tests tsan     # ThreadSanitizer（单独一轮）
./tools/nsotool.py selftest
```

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
make -C plugin
make -C overlay
```

## 工具说明

### `tools/nsotool.py`

纯 Python 的 NSO0 解析器，无第三方依赖。

```bash
./tools/nsotool.py info       work/main.nso   # Build ID + 段布局
./tools/nsotool.py buildid    work/main.nso   # 只打 Build ID
./tools/nsotool.py decompress work/main.nso -o work/main.bin
./tools/nsotool.py selftest                   # LZ4 解压器自检
```

内置 LZ4 块解压器，附 8 个手工构造的测试向量。
会检测 NSO flags bit 7（`UseZbicCompression`，zstd 而非 LZ4）并明确报错，
而不是静默解出垃圾数据。

### `tools/30-gen-offsets.py`

从 `dump.cs` 提取符号。要抓什么在 `tools/targets.json` 里配置，
支持字段别名（`maxHP` / `maxHp` / `MaxHP` 都试）、命名空间消歧、重载检测。

必需符号缺失时以 exit code 2 失败并打印自查命令 —— 包括
Il2CppDumper 报 `RVA: -1`（方法无代码）的情况。

## 技术笔记

### 工具链要点

- Silksong 是 **Unity 6000.0.50f1** → metadata **v31**，
  上游 `Perfare/Il2CppDumper` v6.7.46 支持 v16–v31，**不需要第三方分支**
  （v35–v39 的分支是给 Unity 6000.3+ 的）
- Il2CppDumper **内部会解压 LZ4 NSO**，不需要先跑 hactool
- 需要 NSO/NCA/XCI 处理时，`jakcron/nstool` 有 macOS arm64 预编译版

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
| 3 | seqlock 读取端 | ✅ 8 项测试 + 850 万份并发快照零撕裂 |
| 4 | NSO 解析 / LZ4 解压 | ✅ 8 个测试向量 |
| 5 | 偏移生成器 | ✅ fixture 验证通过 |
| 6 | 游戏转储 + IL2CPP dump | ⏸ 需要主机在手边 |
| 7 | SaltyNX 插件骨架 | ⏳ |
| 8 | TakeDamage hook | ⏳ |
| 9 | Tesla overlay 渲染 | ⏳ |
| 10 | 收集物地图标注 | 📋 v2 |

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
