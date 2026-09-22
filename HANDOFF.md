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
| NSO 解析 / LZ4 | `tools/nsotool.py` | 17 个手工向量自检 |
| 偏移生成 | `tools/30-gen-offsets.py` | fixture + 2 条失败路径 |

随时可跑，不需要游戏转储、不需要 devkitPro：

```bash
make -C tests test     # ASan + UBSan
make -C tests tsan     # ThreadSanitizer
./tools/nsotool.py selftest
```

### 写了但**没有编译验证**

`plugin/source/main.cpp`、`overlay/source/main.cpp`、两个 `Makefile`。

原因：开发机是 macOS arm64，没装 devkitPro，也没装 Docker。
这些文件是纸面正确、未经编译器检验。**接手第一件事就是让它们编译过。**

可测的部分我都刻意拆成了纯函数（重定位、hook 规划、布局、协议读写），
所以 `main.cpp` 里剩下的基本只有 API 调用序列，编译不过大概率是
头文件路径或 API 签名对不上，不是逻辑错。

### 完全没做

- **收集物地图标注**（v2）。数据可以直接复用 PC 端 `Completionist Map` 的
  JSON（游戏内容一致，Switch 的 assetbundle 也能用 AssetStudio 离线提取），
  难的是运行时在地图 UI 上画 pin，要另外 hook 地图相关的类。
- **世界坐标跟随**（怪物头顶血条）。v1 是屏幕固定位置。
  协议里已经预留 `screenX/screenY` + `SSHUD_FLAG_POS_VALID`，**不需要改协议**。

---

## 卡在哪

**唯一的外部阻塞是游戏转储。** 需要主机在手边，用 nxdumptool 导出
ExeFS（拿 `main`）和 RomFS（拿 `global-metadata.dat`）。

拿到之后：

```bash
./tools/00-setup.sh                # 装 devkitPro + .NET
./tools/10-extract.sh <转储目录>    # 输出 Build ID 与段布局
./tools/20-dump-il2cpp.sh          # 跑 Il2CppDumper
./tools/30-gen-offsets.py --build-id <BID> --image-size <size>
```

产出 `plugin/source/offsets.h`（不入库，游戏更新后重新生成）。
在这之前插件编译不过 —— `Makefile` 的 `check-offsets` 会明确提示。

---

## 六个会浪费你时间的坑

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

### 6. overlay 的输入是**复制**，不是移交

overlay 进程是 `AppletType_None`，直接读 HID 共享内存，
所以**游戏和 overlay 会同时收到同一次按键**。
退出组合键选的是 `L + R + 右摇杆按下`（小骑士用不到右摇杆按下）。
加热键时务必避开游戏用到的键。

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

- devkitPro 的 macOS 包是否原生 arm64（官网被 Cloudflare 挡）。
  **确定可用的是 Docker**：`devkitpro/devkita64` 确认有 `linux/arm64` manifest。
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
| 开发机 | 任意 + Docker | `devkitpro/devkita64` 原生支持 arm64 |

安装路径注意大小写：

```
SD:/SaltySD/plugins/010013C00E930000/sshud.elf     ← 大写 (%016lX)
SD:/switch/.overlays/sshud-overlay.ovl
```

`SaltySD/patches/<titleid>/` 用的是小写，别混。

---

## 建议的推进顺序

1. **先让两个 `main.cpp` 编译过**（Docker 路线最省事）。这一步会暴露
   头文件路径和 API 签名的问题。
2. 试 `FindSymbolBuiltin("il2cpp_resolve_icall")` 拿 main 基址，
   能成就去掉 `+0x4000` 的启发式。
3. 转储游戏，跑完流水线拿到 `offsets.h`。
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
