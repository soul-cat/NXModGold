# 接手文档

给下一个接手本项目的人（或 agent）。目标是让你不用重新走一遍调研，
也不用踩已经踩过的坑。

先读这份，再读 `README.md`（架构与流水线），研究细节在 `docs/tasks/`。

---

## 一句话

在已破解的 Switch 1 上给《空洞骑士：丝之歌》做怪物血条 HUD。
PC 端有成熟 mod 但**完全无法移植**（PC 是 Unity Mono，Switch 是 IL2CPP），
所以走原生注入 + ARM64 内联 hook + Tesla overlay 渲染。

---

## 现在能做什么、不能做什么

### 已完成且经过验证

| 模块 | 文件 | 验证方式 |
|---|---|---|
| 共享内存协议 | `shared/hud_protocol.h` | 结构体大小/对齐断言 |
| 敌人追踪器 | `plugin/source/tracker.hpp` | `test_tracker` 10 项 |
| seqlock 读取端 | `overlay/source/hud_reader.hpp` | `test_seqlock` 真线程并发，800 万+ 快照零撕裂 |
| **ARM64 指令重定位** | `plugin/source/arm64_reloc.hpp` | `test_reloc` 4 万余条性质验证，参考编码来自系统汇编器 |
| hook 规划 | `plugin/source/hook_pool.hpp` | `test_hookplan` 6 项 |
| 血条布局 | `overlay/source/hud_layout.hpp` | `test_layout` 11 项 |
| NSO 解析 / LZ4 | `tools/nsotool.py` | 17 个手工向量自检；真实 dump 已与参考 lz4 库逐字节比对 |
| 容器解包(NSP/NSZ/XCI) | `tools/05-unpack-container.sh` + nstool/nsz | 真实 base+update 全流程，NCA 哈希与 cnmt 一致 |
| NCA 压缩分区读取 | `tools/ncaext`(LibHac) | 6.2GB RomFS 完整导出，metadata v31 魔数/版本正确 |
| registration 定位 | `tools/find-il2cpp-registrations.py` | 产出与 .text ADRP 指令对独立验证一致 |
| IL2CPP dump | vendor/Il2CppDumper + `tools/il2cppdumper-unity6.patch` | 37MB dump.cs，18.4 万方法 RVA |
| 偏移生成 | `tools/30-gen-offsets.py` | 字段偏移与 fieldOffsets 原始表独立比对一致 |

随时可跑，不需要游戏转储、不需要 devkitPro：

```bash
make -C tests test     # ASan + UBSan
make -C tests tsan     # ThreadSanitizer
./tools/nsotool.py selftest
```

### 写了且**已编译验证**（2026-09-23，原生 devkitPro / Intel mac）

`plugin/sshud.elf`（strip 后 100KB）与 `overlay/sshud-overlay.ovl`（292KB）
均编译链接成功。编译期修掉的问题：

- trampoline 池的 section 属性必须显式 `"ax"`（volatile 数组默认 RW 属性
  会让链接器**静默丢弃**该 section，实机 Memcpy 时才炸——纸面代码查不出）。
- libtesla 用 **Status-Monitor-Overlay vendored 的 fork**（常驻配方完整、
  无 curl 依赖；README 原来指的 libultrahand 是错误方向，别用）。
  它是"头文件即实现"：一个 .cpp 里 `#define TESLA_INIT_IMPL` 再 include。
- handleInput 的 `touchPosition`/`JoystickPosition` 是 libnx 宏，展开后
  签名是 `(u64, u64, const HidTouchState&, HidAnalogStickState, HidAnalogStickState)`。
- SaltySD 头：插件侧用 NX-FPS 的 include/saltysd + `switch_min.h` shim；
  overlay 侧用 Status-Monitor 的 `SaltyNX.h`（含 Connect/Term 全套）。
- `SaltySD_GetBID()` 无参返回 u64；`SaltySD_Init()` 返回 void（NX-FPS 版）。

### 完全没做

- **收集物地图标注**（v2）。数据可以直接复用 PC 端 `Completionist Map` 的
  JSON（游戏内容一致，Switch 的 assetbundle 也能用 AssetStudio 离线提取），
  难的是运行时在地图 UI 上画 pin，要另外 hook 地图相关的类。
- **世界坐标跟随**（怪物头顶血条）。v1 是屏幕固定位置。
  协议里已经预留 `screenX/screenY` + `SSHUD_FLAG_POS_VALID`，**不需要改协议**。

---

## 卡在哪

**不再卡。** 2026-09-23 全流水线打通：转储解包 → `offsets.h`（Build ID
`FC9EA4CCC955D579`，v1.0.3）→ `sshud.elf` + `sshud-overlay.ovl` 编译成功。
**下一步是实机验证**（先只装 overlay 看"等待插件…"，再装插件看诊断位）。

```bash
./tools/00-setup.sh                  # .NET 等依赖（devkitPro 部分手动）
./tools/05-unpack-container.sh --keys datas/prod.keys <base.nsp> <update.nsz>
./tools/10-extract.sh work/dump      # Build ID + 段布局
./tools/20-dump-il2cpp.sh            # 自动打补丁 + 自动定位 registration
./tools/30-gen-offsets.py --dump work/il2cpp/dump.cs \
    --build-id FC9EA4CCC955D579 --image-size 0x83CE000
```

产出 `plugin/source/offsets.h`（不入库，游戏更新后重新生成）。
**注意：偏移只对应这个 Build ID；实机必须装同一版本，否则 verifyBuildId 拒载。**

---

## 六个会浪费你时间的坑（+ Unity 6 转储四坑，见下）

按踩中概率排序。每条都有一手出处，不是猜的。

### 1. RVA 锚定在模块加载基址，**不减任何东西**

```
运行时地址 = main_base + RVA
```

不要减 `.text` 的 `mem_off`。依据：`Il2Cpp.GetRVA` 默认是恒等函数
（`Il2Cpp.cs:343`），PE 覆写成 `pointer - ImageBase`、Macho64 覆写成
`pointer - vmaddr`，而 **`NSO.cs` 根本没覆写**。

我一开始就是按"减 memOff"写的，实践上因为 memOff 恒为 0 所以是 no-op，
但原则上错。已修。

### 2. `SaltySDCore_getCodeStart()` 返回的是 **rtld，不是 main**

进程布局是 `[Core] [rtld] [main] [subsdk...] [sdk]`，Core 被映射在
rtld 下方 `game_start - 0x200000`，被 `getCodeStart` 的过滤条件排除了。

拿 main 基址只能用 NX-FPS 的启发式走法（`plugin/source/main.cpp:findMainNsoBase`）。
**这是启发式，不是 API 契约。**

> 有个更干净的备选没验证过：`SaltySDCore_FindSymbolBuiltin("il2cpp_resolve_icall")`
> —— Unity 的 main NSO 导出 IL2CPP C API，拿到绝对地址减去该导出的 RVA 就是
> main 基址，不用猜 `+0x4000`。**值得优先花 20 分钟试这个。**

### 3. `SaltySD_Memcpy` 只在插件 `main()` 期间可用

Core 在 `LoadPlugins()`(`main.c:466`) 之后立刻 `SaltySD_Deinit()`(`:496`)。
所有代码 patch 必须在入口一次做完。

能不能之后重新 `SaltySD_Init()` 拿回来？代码上看可行（命名端口一直有人服务），
但 `Handle saltysd` 是 Core 的全局变量，插件靠动态链接共享它，重新 init 会
覆盖 Core 的副本。**仓库里没有任何先例，未验证。**

### 4. 插件 `main()` 跑在游戏执行任何自身代码之前，IL2CPP **未初始化**

对我们无害（只写指令字节，NSO 已映射且 `.text` 已最终确定），
但**任何需要 IL2CPP 运行时状态的事都必须推迟到 hook 里做**。
现在的 hook 体只按固定偏移读字段，不碰 IL2CPP 运行时。

如果你要加需要调 IL2CPP 函数的功能（比如取敌人名字），
不能在 `main()` 里做。

### 5. `main()` 必须 **return**，不能 exit

标准 libnx crt0 在 `b main` 之前把 x30 设成 exit，放任不管会杀掉游戏进程。
已经在 `main.cpp` 里覆写了 `__libnx_exit`，别动那段。
插件也不能用游戏的堆，自带了 `g_fakeHeap`。

### 6. overlay 的输入是**复制**，不是移交；退出键就是 Tesla 组合键

overlay 进程是 `AppletType_None`，直接读 HID 共享内存，
所以**游戏和 overlay 会同时收到同一次按键**。加热键时务必避开游戏用到的键。

显隐/退出的正确方式（vendored tesla 的 hidPoller 在输入线程层面处理，
FullMode=false 的输入穿透模式下 Gui::handleInput 根本不会被调用，
别往里加退出逻辑——实机验证过无效）：
  - HUD 显示中按 **Tesla 呼出组合键（L+↓+右摇杆按下）或 Home 键** → 隐藏
  - 隐藏后再按组合键 → 恢复
  - 常驻期间组合键被本 overlay 独占，Tesla-Menu 不会弹出（预期行为）
  - 挂起状态下按键无效（整个进程冻结），必须在游戏运行中操作

---

## 必须保持的不变量

改代码时如果动到这些，对应的测试会红。**测试红了先怀疑自己，别改测试。**

1. **重定位后目标地址不变。** `test_reloc` 的核心性质。改 `arm64_reloc.hpp` 时
   这条必须继续成立。
2. **拒绝优于半对。** 任何搬不动的指令都必须让 `buildTrampoline` / `planHook`
   返回失败，且**不产生半成品输出**（`test_hookplan` 专门测了这点）。
   生成一个"大概能用"的 trampoline 会让游戏随机崩溃，比不装 hook 糟得多。
3. **seqlock 快照一致性。** `test_seqlock` 按写者不变量逐条校验。
   动 `tracker.hpp` 的 `publish()` 或 `hud_reader.hpp` 的 `snapshot()` 时注意。
4. **所有血条落在 framebuffer 内。** `test_layout` 会验证。
5. **Build ID 不匹配就什么都不做。** 拿错版本的偏移去写代码段会直接崩游戏。
   `main.cpp:verifyBuildId` 是最后一道闸。

### Unity 6 转储四坑（2026-09 一手踩坑记录，全部已解）

1. **NCA 压缩分区（固件 16+）**：Silksong 的 RomFS 用了 CompressionInfo 压缩存储，
   nstool 连最新开发分支都不支持（只写了 sparse 不支持，压缩压根没实现）。
   任何"Hash layer N failed hash verification"都是这个。**解法：`tools/ncaext`**
   （C# + LibHac `Ryujinx.LibHac` 0.20.0 NuGet，模拟器同源实现）。注意 LibHac 的
   ExternalKeySet 存**加密**titlekey（GetDecryptedTitleKey 内部解），注入解密后的会全盘乱码。
2. **更新档 RomFS 是增量（BKTR/AesCtrEx）**：必须叠在 base 之上重建
   （LibHac `OpenFileSystemWithPatch`）。base NSP 和 update NSZ 都要给。
   NSZ 要先经 nsz 工具（venv 自动建）解回 NSP。
3. **Il2CppDumper/Il2CppInspector(Redux) 自动搜索全灭**：Unity 6 把 metadata 的
   image 数（78，含 `__Generated`）与二进制 codeGenModules 数（80）解耦了，
   工具的 `count == imageCount` 硬校验永远失败。**解法：`tools/find-il2cpp-registrations.py`**
   定位两个 registration 地址喂手动模式；`20-dump-il2cpp.sh` 已自动化此流程。
   该脚本还会自动应用 `tools/il2cppdumper-unity6.patch`（逐类型容错 + codegen
   附加类型占位——否则一个类型炸掉整个 image 的输出）。
4. **Unity 6 有 786 个 codegen 附加类型**（klassIndex ≥ metadata typeDefs.Length），
   另有 `__Generated` 附加模块。都无害，patch 后按占位处理。

其它细节：metadata 里 `HitInstance` 等结构在 Switch RomFS 的路径是扁平的
（`/Data/Managed/Metadata/global-metadata.dat`）；bash 3.2（macOS 自带）会把
全角括号当变量名字符——`$var（`必须写成 `${var}（`；nstool 解不出分区时
**返回 0**，只能靠产物判断成败。

---

## 有风险的地方（实机才能验证）

### trampoline 池这条路没有先例

拿不到 RWX，所以 trampoline 放在插件自己的 `.text`（被映射为 RX），
用 `SaltySD_Memcpy` 走 debug SVC 绕过页权限写入。

调研原话：**"I found NO in-tree example of either. Both are my constructions,
not verified patterns."** —— NX-FPS 和 lsfg-nx 全是 GOT 导入表替换，
不需要 trampoline，所以整个 SaltyNX 生态没人走过这条路。

**实机首次运行就是这条路的验证点。** 如果不成，退路是 exlaunch
（SaltyNX 作者自己在 README 里推荐的，有真正的内联 hooker）。

### 指令缓存维护未确认

`SaltySD_Memcpy` 写 `.text` 之后的 icache 维护，调研没能从源码确认。
内核层面应该处理，Core 自己的 SVC patch 在生产环境能用 —— 属于经验证据。
如果 hook 装上去没反应但地址没错，怀疑这个。

### `initHp` 没人在运行时真读过

最大血量字段是 `initHp`（**没有** `maxHP`/`MaxHP`）。
声明确认存在，也确认可按名访问，但公开的 PC 血条 mod 压根没读它
（它用 `Mathf.Max(打前, 打后)` 推断）。

追踪器两条路都支持：读到就用真值，读不到退化为推断。所以就算 `initHp`
在运行时拿不到可用值，血条仍然能工作，只是最大值要打几下才收敛。

### 其他未确认项

- devkitPro 原生 macOS 包的可用性（官网被 Cloudflare 挡，无法离线验证；
  Intel x86_64 历史上受支持，但维护者已不常测 Intel mac，2026 现状未知，
  装上 dkp-pacman 跑一次 `dkp-pacman -S switch-dev` 即知）。
  **确定可用的是 Docker**：`devkitpro/devkita64` 的 `linux/amd64` 与
  `linux/arm64` manifest 均已验证（2026-09，Docker Hub API）。
  Intel Mac 上容器原生运行，无仿真开销。
- Silksong 的 NSO 是否用 zstd/zbic 压缩（header flag bit 7）。
  若用了，`nsotool.py`、Il2CppDumper、Il2CppInspectorRedux **三者全部卡住**，
  因为都只实现了 LZ4。`nsotool.py` 会明确报错而不是解出垃圾。
- SaltyNX 1.9.3 在固件 21.x/22.x 上的表现。

---

## 环境要求

| 项 | 要求 | 备注 |
|---|---|---|
| 主机 | Switch 1（Erista/Mariko）+ Atmosphère | **Switch 2 没有公开 CFW，本项目不适用** |
| SaltyNX | **≥ 1.7.3** | 1.5.0 才修好 Unity 6（更新日志点名 Silksong），1.7.3 正式修了 Unity 6 依赖的 `GetAllModuleInfo` |
| libtesla | **ppkantorski/libultrahand** | 上游 WerWolv/libtesla 做不到常驻 + 输入穿透，`overlay/Makefile` 会检查 |
| Il2CppDumper | 上游 Perfare v6.7.46 | Silksong 是 Unity 6000.0.50f1 → metadata v31，**不需要第三方分支** |
| 开发机 | 任意 + Docker | `devkitpro/devkita64` 有 linux/amd64 与 linux/arm64 双 manifest（2026-09 验证） |

安装路径注意大小写：

```
SD:/SaltySD/plugins/010013C00E930000/sshud.elf     ← 大写 (%016lX)
SD:/switch/.overlays/sshud-overlay.ovl
```

`SaltySD/patches/<titleid>/` 用的是小写，别混。

---

## 建议的推进顺序

1. **先让两个 `main.cpp` 编译过**（原生 devkitPro：装 dkp-pacman.pkg +
   `dkp-pacman -S switch-dev`；Intel mac 上 Docker amd64 是已验证的退路）。
2. 试 `FindSymbolBuiltin("il2cpp_resolve_icall")` 拿 main 基址，
   能成就去掉 `+0x4000` 的启发式。
3. ~~转储游戏~~ **已完成**（v1.0.3，Build ID `FC9EA4CCC955D579`，offsets.h 在位）。
   游戏更新后重跑 05→10→20→30 四步即可。
4. **先只装 overlay，不装插件** —— 应该显示"等待插件…"。
   这验证了 overlay 的常驻层和 libtesla 配置没问题。
5. 再装插件。看 overlay 显示的诊断走到哪一步
   （`SSHUD_HOOK_INIT_OK` → `BUILDID_OK` → `TAKEDAMAGE`）。
   这三个位就是为了让你不用猜卡在哪。
6. 打一个已知血量的敌人，核对数值。

**联机会封号。** 装任何 CFW 插件后不要联网，用 90DNS 或飞行模式。

---

## 研究资料在哪

`docs/tasks/` 下三份，都标注了一手出处和**未能确认的项**：

- `research-overlay.md` —— libtesla 常驻层、输入穿透、坐标空间、dmnt:cht
- `research-il2cpp.md` —— RVA 语义、Silksong 类结构、工具链
- `hud-v1.md` —— v1 范围决策与验证清单

`docs/CHANGELOG.md` 是决策记录，`docs/agent-silksong-nx-hud-job.md` 是当时的分工。

每份研究里"未能确认"那一节值得认真看 —— 那是已知的未知，
比不知道自己不知道强。
