# CHANGELOG

## 2026-09-22

### feat(hud): 项目奠基 — 共享内存协议 / 追踪器 / 偏移生成器

确定技术路线：Switch 版 Silksong 为 Unity IL2CPP 构建，PC 端 BepInEx mod
（ShowDamage HealthBar、Completionist Map）无法移植，必须走原生注入 + ARM64 内联 hook。

**新增**

| 文件 | 说明 |
|------|------|
| `shared/hud_protocol.h` | 插件↔overlay 共享内存协议。seqlock 单写多读，归一化屏幕坐标 |
| `plugin/source/tracker.hpp` | 敌人状态追踪器。定长 24 槽，无分配，LRU + TTL 淘汰 |
| `tools/00-setup.sh` | devkitPro + .NET + Il2CppDumper 环境准备 |
| `tools/targets.json` | 需要从 dump.cs 提取的类/字段/方法清单 |
| `tools/30-gen-offsets.py` | dump.cs → `plugin/source/offsets.h` |
| `tests/test_tracker.cpp` | 10 项宿主机单元测试 |

**验证**

```
make -C tests test   →  10/10 通过（ASan + UBSan）
./tools/30-gen-offsets.py --dump <fixture>  →  13 个符号正确提取
```

**关键设计决策**

1. **归一化坐标 [0,1]** —— 插件不感知 overlay 分辨率，overlay 不感知游戏分辨率。
   掌机/底座切换、第三方分辨率 mod 都不破坏对齐。
2. **seqlock 而非互斥锁** —— hook 跑在游戏主线程，绝不能因 overlay 阻塞。
3. **maxHp 推断回退** —— 部分敌人读不到 maxHP 字段时，用首次观测 `hp + damage` 估计。
4. **所有 RVA 隔离在生成的 `offsets.h`** —— 该文件不入库，游戏更新后重新生成即可。
5. **v1 只做屏幕固定血条**，不做世界坐标跟随。理由见 `docs/tasks/hud-v1.md`。

**未决**（等待研究结论）
- SaltyNX 插件入口与 main NSO 基址获取 API
- libtesla 常驻层 / 输入穿透的具体实现
- Il2CppDumper 对 NSO 的 RVA 语义（相对什么基准）

---

## 2026-09-22（续）

### feat(plugin): ARM64 指令重定位 + hook 安装

SaltyNX 不提供内联 hook（全仓库 grep "hook" 零命中），NX-FPS 与 lsfg-nx
全是 GOT 导入表替换 —— 对 `TakeDamage` 这种 main 内部直接 `bl` 调用的函数
完全无效（没有 GOT 槽，没有 `.dynsym` 条目）。因此自己实现。

**设计立场：宁可拒绝，也不生成半对的代码。**

| 指令 | 处理 |
|---|---|
| `B` / `BL`（±128 MiB） | 重写立即数 |
| `ADR`（±1 MiB）/ `ADRP`（±4 GiB） | 重写立即数 |
| `B.cond` / `CBZ` / `TBZ` / `LDR-literal` | 一律拒绝 |

测试两层：参考编码来自 macOS 系统汇编器真实输出（`tests/gen_ground_truth.sh`
可复现），验证独立实现的解码器；再用解码器做性质测试 —— 重定位后解析出的
目标地址必须与重定位前完全一致。

**编译器抓到一个真 bug**：`-(int64_t)1 << 27` 实际是 `(-1) << 27`，
一元负号优先级高于移位。三个范围常量全中。

**测试抓到一个测试自身的 bug**：ADR 性质测试只有 34/20000 通过。
查下来是期望不切实际 —— ADR 范围 ±1MiB，而 from/to 撒在 1GiB 范围里。
结论值得记：**序言里出现 ADR，基本等于放弃该 hook**。

### feat(plugin): 入口与 hook 安装

三个硬约束支配了设计：

1. 拿不到 RWX（内核只给 R/RW/RX，`svcCreateCodeMemory` 零售 NPDM 里没有）
   → trampoline 池编译期预留在插件自己的 `.text`（被映射为 RX），
   用 `SaltySD_Memcpy` 走 debug SVC 绕过页权限写入。
   **此路生态内无先例**，实机首次运行即验证点。
2. `SaltySD_Memcpy` 只在 `main()` 期间可用（Core 在 `LoadPlugins` 后立刻 `Deinit`）
   → 所有 patch 一次装完。
3. `main()` 跑在游戏执行任何自身代码之前，IL2CPP 未初始化
   → hook 体只按固定偏移读字段，不碰 IL2CPP 运行时。

入口三个讲究：必须 return 不能 exit、自带 fake heap、`_start` 在 ELF 偏移 0。

### feat(overlay): 常驻层渲染

配方来自 Status-Monitor-Overlay 的 Mini 模式（`FullMode=false` /
`TeslaFPS` / `deactivateOriginalFooter` / `alphabackground=0x0` /
`requestForeground(false)`，且**永不调用 `hide()`**）。

framebuffer 选 1280x256：双缓冲 1280 KB，对 nx-ovlloader 的 6 MB applet 堆
是安全余量（全屏 720 要 3.6 MB，太紧）。宽度必须是 32 的倍数。

布局全部做成纯函数（`hud_layout.hpp`），实机上只剩 `drawRect` 调用。
11 项测试覆盖淡出曲线单调性、配色阈值、填充宽度夹取、条不越界、
同列不重叠、Boss/普通分列、过期隐藏、排序、防御性输入。

### chore: docs/ 纳入版本库

用户明确指示。研究结论与决策记录对接手者有价值，
且仓库为私有。`CLAUDE.md` 仍留在本地。

### docs: 新增 HANDOFF.md

给下一个接手者的文档：当前状态（哪些验证过、哪些没有）、
唯一外部阻塞、六个会浪费时间的坑（每条带一手出处）、
必须保持的不变量、有风险待实机验证的地方、建议推进顺序。

---

## 2026-09-24

### chore: 迁到 M1 Pro 开发机

devkitPro 原生 arm64（`devkitpro-pacman-installer.pkg` v6.0.2 内含 arm64 子包，
工具链从 `osx/arm64` 仓库装，GCC 16.1）。`make -C overlay` 直接通过；
`make -C plugin` 用桩 offsets.h 编译通过（重定位 410×RELATIVE + 6×GLOB_DAT +
1×JUMP_SLOT，6 个 UND SaltySD 符号），产物已删。宿主机测试 `make -C tests test` 全绿。

换机踩到两处 HANDOFF 未记录的步骤，已补进 HANDOFF：
- `saltysd_ipc_sm.h` 是 Status-Monitor `SaltyNX.h` 的改名拷贝，此前只存在于旧机。
- libsysbase 修补必须用 `aarch64-none-elf-ar`，macOS `ar` 读不出成员。

### feat(exl): exlaunch 注入路线（模拟器可测）

Ryujinx 不跑 Atmosphère sysmodule，SaltyNX 插件与 Tesla overlay 在模拟器里
不存在。新增 `exl/`：subsdk9 + main.npdm 从 exefs 注入，Ryujinx 与实机通用。

| 文件 | 说明 |
|------|------|
| `exl/program/main.cpp` | Build ID 校验（.rodata GNU note）+ TakeDamage trampoline hook，只打日志 |
| `exl/Makefile` / `exl/config.mk` | vendor/exlaunch 同步到 build/sshud/ 后覆盖 program/，不改 vendor |
| `tools/40-patch-npdm.py` | 游戏原版 main.npdm 的 ACID/ACI0 KAC 补 4 个 SVC，其余字节不动 |
| `tools/05-unpack-container.sh` | 同时拷出 main.npdm，列出 exefs，自带 subsdk9 时告警 |

**验证**

```
./tools/40-patch-npdm.py selftest   → 与 npdmtool 逐字节一致 / 幂等 / 子段后移 / ACI0 在前布局
make -C exl（桩 offsets.h + 伪造 npdm）→ subsdk9 62 KB，deploy/ 组装成功，产物已删
objdump TakeDamageHook::Callback      → x0/x1/x2 保存后原样尾调 Orig
```

**决策**
- 不用 exlaunch 模板 json 生成 npdm：那是别的游戏的栈大小/服务表，改为在原版上最小补丁。
- 本版不绘制，先在 Ryujinx 日志里确认 hook 触发与 hp/initHp 数值，再决定
  绘制走游戏内（NVN/ImGui，d3hack 有先例）还是继续实机 Tesla。

**未决**：模拟器下载源 `update.ryujinx.app` / `git.ryujinx.app`（91.239.78.255）
2026-09-24 全球超时，待恢复。

---

## 2026-10-24

### feat(exl): 与 SilkModLoader 皮肤 Mod 共存（v1.0.8）

皮肤 Mod 和 HUD 原先抢同一个 `exefs/subsdk9` + `main.npdm`。现在改成两个
exefs 模块 + 一份共享 npdm，不改任何一方的代码：

| 改动 | 说明 |
|------|------|
| `exefs/subsdk8` | 皮肤运行时（原包 `subsdk9` 改名，必须排在 HUD 前） |
| `exefs/subsdk9` | SilksongHUD，保持不变 |
| `main.npdm` | union：原版 + 9 个 SVC（含皮肤需要的 `0x4B/0x4C/0x73/0x77/0x78`）+ fs 全开；两个包逐字节相同 |
| 皮肤 IPS | 原样使用；实机确认 `SilkModLoader - main = 0x8A3A000`，与 IPS 常量一致 |

- `tools/40-patch-npdm.py`：`REQUIRED_SVCS` 增加 5 个 code-memory SVC，生成 union npdm。
- `exl/program/main.cpp`：启动时打印 rtld 模块表，便于以后确认皮肤模块基址。
- `tools/85-repack-skin-coexist.py`：只改容器布局（subsdk9→subsdk8、替换 main.npdm），
  保留原始皮肤资源与 IPS。
- `docs/coexist-skin.md`：布局、TOMOD 冲突规则、实机验证记录、版权说明。
- 修正 `work/` 里的 v1.0.3 metadata（17,153,796 字节）并重新生成 offsets；
  `SpriteRenderer.set_sprite=0x5F3F650` 等已知值与 SilkRuntime 一致。

**实机验证**（v1.0.3，Build ID `FC9EA4CCC955D579`）：模块表
`main=0x930a06000`、`SilkModLoader=0x939440000`、`sshud=0x93a089000`；
`map hooks installed: 571 collectibles`；存档 1 与 4 均正常，皮肤与 HUD 同时生效。

### feat(hud): 皮肤主题（v1.0.9）

检测到 SilkModLoader 时，血条自动切换到皮肤配色；没有皮肤时保持原配色。

| 文件 | 说明 |
|------|------|
| `exl/program/hud_theme_model.hpp` | 默认/皮肤两套调色板；65%→35% smoothstep 低血量过渡；`bossText` 预留 |
| `exl/program/hud_theme.hpp` | 扫 rtld 模块表找 `SilkModLoader`，拿不到名字时用 `Subsdk1 != 自己` 兜底 |
| `exl/program/hud_draw.hpp` | 血条/数字/伤害/菜单颜色全部走 `activeTheme()`；填充顶部 1px 高光；删除条上方重复的 BOSS 名 |
| `tests/test_hud_theme.cpp` | 主题选择、调色板、低血量过渡与颜色混合 |

- 皮肤配色：青蓝填充 `#55C8F0`、暗青底 `#0A1418`、淡青外框 `#D8F4FF`、低血量暗红 `#C03050`、伤害 `#FF4A6A`。
- 启动日志：`bar theme: skin|default`；构建标记 `hud build: theme-v3`。
- 地图收集物恢复为完整构建（571 条）；Completionist Map 数据从 Thunderstore
  `Yoraiz0r/Completionist_Map/1.0.3` 重新获取，生成 `collectibles.gen.h`（gitignore）。
- 实机 2026-10-24：皮肤 + HUD 共存，存档 1/4 正常，571 地图标记、主题血条均生效。

### docs: README 瘦身与使用说明

README 只保留用户要看的：效果图、功能、安装、操作、皮肤共存与文档索引；
开发内容拆到独立文档，并补上实机截图与操作示意图。

| 文件 | 说明 |
|------|------|
| `README.md` | 顶部 4 张实机效果图；功能表精简；新增「使用」一节 |
| `docs/usage.md` | 完整使用说明：开关与 `config.ini`、九个纹章、BOSS 挑战、地图规则、存档导入导出、主题参数、日志与限制 |
| `docs/development.md` | 架构、目录、构建、测试、地址换算、流水线（从 README 迁出） |
| `docs/images/controls.svg` / `.png` | Switch 掌机操作示意图，线条标注 R3 / 方向键 / ZR / A·B |
| `docs/images/shot-*.jpg` | 实机截图：皮肤主题、BOSS 条、选项页开关、BOSS 挑战列表 |
| `HANDOFF.md` | 接手入口改为指向 `docs/development.md` |

- 修正选项页行数表述：新增的是七行（原文一处写成「六行」）。
- 起始纹章顺序与 `start_crest_model.hpp` 对齐：猎人、收割者、流浪者、野兽、女巫、建筑师、萨满、诅咒、无斗篷。

### release: TOMOD 商店上架 HUD 与共存版皮肤

| Mod | 操作 | 结果 |
|---|---|---|
| `1409` 丝之歌 血条与地图 HUD | 文件更新到 v1.0.9、改名、重写介绍、补 2 张截图 | 已上架，内部版本 2，文件 147,115 B |
| `1661` 丝之歌 × 星见雅皮肤（HUD 共存版） | 新上架，`skin` 类型 | 已上架，内部版本 1，文件 179,740,213 B |

- 商店包改成 TOMOD 原生结构：zip 根目录直接是 `exefs/`、`romfs/`、`exefs_patches/`，
  由 TOMOD 自己包装到 `atmosphere/contents/<tid>/`；GitHub 发布包仍是手动解压用的 `atmosphere/` 结构。
- 两个包的 `exefs/main.npdm` 逐字节相同（`7edba565…`，1612 B），TOMOD 走引用计数，顺序不限。
- 皮肤作者标注：终焉苍龙（原皮肤）/ nice2cu093（Switch 移植、SilkRuntime）；介绍里写明只改容器布局。
- HUD 的游戏版本从 `1.0.3` 修正为 `1.0.30000`，与商店其它 Mod 一致。
