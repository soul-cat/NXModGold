# NXModGold

在已破解的 Nintendo Switch 上为《空洞骑士：丝之歌》实现 PC 端 mod 才有的 HUD 与地图功能。

| | |
|---|---|
| 目标游戏 | Hollow Knight: Silksong v1.0.3 |
| Title ID | `010013C00E930000` |
| Build ID | `FC9EA4CCC955D579`（偏移只对这个版本有效） |
| 目标平台 | Switch 1（Erista / Mariko）+ Atmosphère；Ryujinx 也能加载同一份产物 |
| 注入方式 | exlaunch：`subsdk9` + 修补过的 `main.npdm`，经 Atmosphère exefs 注入 |

接手先读 `HANDOFF.md`（当前状态、实机验证清单、坑）。

## 功能

| 功能 | 行为 | 对标 PC mod |
|---|---|---|
| 敌人血条 | 受玩家伤害的敌人头顶显示血条与 `hp/maxHp`，最后一次掉血 1.5 秒后隐藏 | `Silksong_HealthBar` |
| 伤害数字 | 受击处飘出红色 `-伤害`，2 秒内上升并淡出 | 同上 |
| BOSS 血条 | 最大血量 > 105 的敌人改画屏幕底部居中的 BOSS 条（带猎人日志名字），多个 BOSS 向上堆叠；BOSS 大标题出现（`DisplayBossTitle`）时就登记，不等第一刀；不按时间隐藏，死亡 / 停用 / 离开场景才消失。外框与填充是作者自绘贴图 `exl/assets/boss_frame.png` / `boss_fill.png` | 同上 |
| 地图收集物标注 | 571 个收集物（18 类）在快速地图与大地图上用游戏自带图标标出；区域地图没买、房间没绘制、已收集的都不显示；已向跳蚤商队猎人买了原生跳蚤图钉的区域，跳蚤交给游戏自己标，不重复画 | `Completionist Map` |
| 游戏内开关面板 | 按住 R3 弹出中文面板，按住期间方向键切换功能，设置存 SD 卡 | — |
| 原生选项页开关 | 「选项 → 游戏」页末尾多出七行，外观、字体、手柄导航与原生选项一致，与 R3 面板共用设置；行多时自动压行距并等比缩小整页，保证都在屏幕内 | — |
| R3 快捷面板开关 | 选项页第五行，默认开：关掉后按住 R3 不再弹面板、方向键不再切换 | — |
| 送货不损坏 | 选项页第六行，默认关：信使送货的物品挨打、撞墙、硬着陆、计时、掉进危险地形都不掉耐久也不碎，交货照常走游戏流程 | — |
| 新存档起始纹章 | 选项页第七行，默认猎人（原版）：9 个纹章任选，只影响之后新开的存档；进第三幕时游戏强制装猎人纹章，没有猎人的存档改为保持当前纹章 | `StartCrestSelector` |
| SD 日志 | `SD:/config/sshud/log.txt`，每次启动覆盖 | — |
| 存档导出 / 导入 | 触发文件驱动，把整个存档导出到 SD，或从 SD 导入 `user1..4.dat` | — |

## 为什么不能直接移植 PC mod

PC 端的 mod 全部基于 BepInEx + Harmony，在 Switch 上一个都用不了：

| | PC 版 | Switch 版 |
|---|---|---|
| Unity 脚本后端 | Mono | IL2CPP |
| 托管程序集 | `Assembly-CSharp.dll` 存在 | 无，C# 已 AOT 编译成 ARM64 进 `exefs/main` |
| 运行时 patch | BepInEx + Harmony | 无 |

Atmosphère 的金手指 VM 只能读写内存，没有绘图能力；血条和地图标注的本质是创建并渲染新的 UI。
所以只能原生注入 + ARM64 内联 hook，在游戏进程里直接调用 IL2CPP 编译出来的 Unity 方法。

## 架构

```
游戏进程（Silksong main NSO）
  rtld 以 subsdk9 加载 sshud（exlaunch）
  exl_main：校验 Build ID → 装 hook（此时 IL2CPP 未初始化，只装 hook 不调 Unity）
  │
  ├─ HealthManager.TakeDamage        打前/打后读 hp，记录掉血与飘字（含 sendDamageTo 转发，只记玩家伤害）
  ├─ DisplayBossTitle.OnEnter        开 5 秒窗口，扫 HealthManager._activeHealthManagers 登记 BOSS
  ├─ CameraController.LateUpdate     每帧：首帧挂 SD 日志 → 存档导出 → 存档导入；读手柄（R3 面板）；
  │                                  扫 BOSS；确保 PlayMakerGUI 存在
  ├─ PlayMakerGUI.OnGUI              Repaint 事件里用 Unity IMGUI 画 BOSS 条 / 敌人血条 / 飘字 / 开关面板
  ├─ GameMap.SetupMapMarkers         开图时刷新收集物标记
  │  GameMap.EnableUnlockedAreas     （两处都刷新，见 HANDOFF）
  ├─ UIManager.UIGoToGameOptionsMenu 打开「选项 → 游戏」前：首次克隆插入四个开关行，之后刷新行上的值
  ├─ MenuButtonList.SetupActive      页面激活重排导航后，把四行接进上下导航
  └─ MenuOptionHorizontal.UpdateSetting  左右切换后：是我们的行就改开关、写回 config.ini
```

所有 hook 体都在 Unity 主线程上跑，`HudModel` 等共享状态不加锁。
地址约定：运行时地址 = main 模块基址 + RVA，RVA 全部来自生成的 `exl/program/offsets.h`，代码里不硬编码。

## 目录

| 路径 | 用途 |
|---|---|
| `exl/program/main.cpp` | 入口：Build ID 校验（从 main `.rodata` 的 GNU build-id note 取），装 hook |
| `exl/program/hud_hooks.hpp` | TakeDamage / LateUpdate / OnGUI 三个 hook |
| `exl/program/hud_model.hpp` | 血条纯逻辑：敌人槽位（16）、飘字池（32）、布局；宿主机可测 |
| `exl/program/hud_draw.hpp` | IMGUI 绘制：敌人血条、BOSS 条、飘字、开关面板 |
| `exl/program/boss_detect.hpp` | `DisplayBossTitle` hook + 激活敌人列表扫描 |
| `exl/program/bar_textures.hpp` | 运行时建 `Texture2D`，三宫格拉伸画 BOSS 条 |
| `exl/program/menu_textures.hpp` | 开关面板中文文字贴图 |
| `exl/program/toggles*.hpp/.cpp` | 开关面板：输入状态机（纯逻辑）、`nn::hid` 读手柄、`config.ini` 读写 |
| `exl/program/map_*.hpp` | 地图标记：建标记、图标、已收集判定、场景缓存、耗时统计 |
| `exl/program/save_export.*` / `save_import.*` / `fs_util.*` | 存档导出 / 导入与 `nn::fs` 复制工具 |
| `exl/program/sd_logger.*` / `loggers.hpp` | SD 卡日志 + `svcOutputDebugString` |
| `exl/program/unity_api.hpp` | 从原生代码调 IL2CPP 方法的薄封装 |
| `exl/assets/` | BOSS 条贴图（作者自绘）与 `menu/*.png` 面板文字贴图 |
| `exl/patches/rtld-sdk20-header.patch` | exlaunch 模块头修补（SDK 20+ rtld 兼容） |
| `exl/Makefile` / `exl/config.mk` | 构建：同步 `vendor/exlaunch` 到 `exl/build/sshud/`，覆盖 `program/`，打补丁，编译，组装 `exl/deploy/exefs/` |
| `tools/` | macOS 侧流水线：解包 → IL2CPP dump → 生成偏移 / 收集物表 / 贴图，补 npdm |
| `tests/` | 宿主机单元测试（不需要 devkitPro） |
| `docs/tasks/` | 研究记录（含已弃用路线的历史资料） |

## 构建

### 1. 环境（一次性）

```bash
./tools/00-setup.sh     # 检查 devkitPro、装 .NET、拉 Il2CppDumper 到 vendor/
git clone --depth 1 https://github.com/shadowninja108/exlaunch.git vendor/exlaunch
export DEVKITPRO=/opt/devkitpro
```

devkitPro 需要 `switch-dev`（`00-setup.sh` 会打印需要 sudo 的安装命令）。

### 2. 从自己的游戏生成偏移（游戏每次更新后重跑）

```bash
./tools/05-unpack-container.sh --keys <prod.keys> <base.nsp> <update.nsz>   # → work/dump/
./tools/10-extract.sh work/dump        # 暂存 main.nso / global-metadata.dat，写 work/BUILD_ID，解压出 work/main.bin
./tools/20-dump-il2cpp.sh              # Il2CppDumper（自动打 Unity 6 补丁、自动定位 registration）
./tools/30-gen-offsets.py --dump work/il2cpp/dump.cs --script work/il2cpp/script.json \
    --main-bin work/main.bin --build-id "$(cat work/BUILD_ID)"               # → exl/program/offsets.h
./tools/50-gen-collectibles.py         # 读 work/completionist/*.zip → exl/program/collectibles.gen.h
```

- 漏了 `--build-id` 时 `SS_BUILD_ID_STR` 为空，运行时 Build ID 校验失败（日志 `MISMATCH`），不装任何 hook。
- `50-gen-collectibles.py` 需要自己下载的 Completionist Map 包（v1.0.3，无 license，数据与生成物都不入库）。
  没有 `collectibles.gen.h` 也能编译，只是地图标注关闭。

### 3. 编译

```bash
make -C exl
```

需要 `DEVKITPRO`、`vendor/exlaunch`、`exl/program/offsets.h`、`work/dump/ExeFS/main.npdm`（来自 05 脚本）。
构建时会自动：用 `tools/60-gen-bar-textures.py` 把 `exl/assets/` 下的 PNG 生成 `bar_textures.gen.h`；
用 `tools/40-patch-npdm.py` 在游戏原版 npdm 上补 exlaunch 需要的 SVC 与 SD 访问权限。
产物：`exl/deploy/exefs/{subsdk9,main.npdm}`。

打包发布用的 zip（解压到 SD 根目录或交给 NX-Mod-Manager 均可，作者 cool）：

```bash
./tools/70-package-release.sh 1.0.0   # → work/release/SilksongHUD-v1.0.0.zip
```

面板文字要改时才需要重渲染（需要 Pillow 与 Noto Sans SC Bold，SIL OFL）：

```bash
python3 tools/61-render-menu-text.py --font work/fonts/NotoSansSC-Bold.otf   # → exl/assets/menu/*.png（入库）
```

### 4. 测试

```bash
make -C tests test     # ASan + UBSan
```

| 测试 | 覆盖 |
|---|---|
| `test_hud_model` | 槽位复用与淘汰、隐藏延时、BOSS 条不超时、BOSS 门槛、布局、飘字进度与回收 |
| `test_toggles` | R3 面板输入（按住、方向映射、按下沿、同时按、松开）、`config.ini` 格式化与解析 |
| `test_options_menu_model` | 原生选项页：从本地化键认出「开」是第几项、按游戏语言选标签、行键名、行距 / 缩放换算、纹章后备名判定、UTF-16 → ASCII 日志转换 |
| `test_map_timing` | 地图刷新耗时的批量汇总 |

工具自检：`./tools/nsotool.py selftest`、`./tools/40-patch-npdm.py selftest`、
`./tools/50-gen-collectibles.py selftest`、`./tools/60-gen-bar-textures.py selftest`。

## 安装

```
SD:/atmosphere/contents/010013C00E930000/exefs/subsdk9
SD:/atmosphere/contents/010013C00E930000/exefs/main.npdm
```

主机上必须装 v1.0.3 更新档（Build ID 对不上时 mod 不装 hook，游戏照常运行）。
游戏自带 exefs 若已有 `subsdk9` 会冲突，05 脚本会告警。

Ryujinx：右键游戏 →「打开 Atmosphere Mods 目录」，放到 `<该目录>/sshud/exefs/`。模拟器设置要求见 `HANDOFF.md`。

## 使用

### 开关

两处都能改，共用同一份 `config.ini`：

- **原生选项页**：「选项 → 游戏」页最后六行（小怪血条 / BOSS 血条 / 伤害数字 / 地图收集物 / R3 快捷面板 / 送货不损坏 / 新存档起始纹章），左右切换即生效。R3 快捷面板（`quick_panel`，默认开）、送货不损坏（`protect_delivery`，默认关）与新存档起始纹章（`start_crest=0..8`，默认 0 猎人）只在这里，R3 面板里没有。游戏语言为简体中文时显示中文标签，否则英文；开 / 关沿用游戏自己的本地化文字。
- **R3 面板**：游戏中随时可用，见下。

按住 R3（右摇杆按下）弹出面板，按住期间按方向键切换，松开 R3 面板消失：

| 方向键 | 功能 | `config.ini` 键 |
|---|---|---|
| ↑ | 敌人血条 | `enemy_bars` |
| → | BOSS 血条 | `boss_bar` |
| ↓ | 伤害数字 | `damage_numbers` |
| ← | 地图收集物标记 | `map_markers` |

默认全开。有改动时松开 R3 写回 `SD:/config/sshud/config.ini`（每行 `key=0/1`，可手改）。
地图标记开关在下次打开地图时生效。只读手柄状态，不拦截游戏输入。
游戏自己的地图 pin 显示设为「无」时，收集物标记也一起隐藏。

### 存档导出

在 SD 上创建空文件 `SD:/config/sshud/export.request`，启动游戏后首帧把整个 `save:/`（含 `Restore_Points*` 子目录）
复制到 `SD:/config/sshud/export/<n>/`（`n` 取第一个不存在的编号，旧导出不覆盖），然后删除触发文件。只读存档。

### 存档导入

把存档放到 `SD:/config/sshud/import/user{1..4}.dat`，启动游戏后首帧（在导出之后）：

1. 原 `save:/userN.dat` 备份到 `SD:/config/sshud/import/backup-userN.dat`；
2. 复制进 `save:/userN.dat` 并 `CommitSaveData`；
3. 导入文件改名为 `userN.dat.done`，下次启动不再重复。

每一步的结果都写进 SD 日志。

## 地址换算

```
运行时地址 = 模块基址 + RVA          ← 不减任何东西
```

Il2CppDumper 对 NSO 报的 RVA 锚定在模块加载基址，不是 `.text` 的 `mem_off`
（`Il2Cpp.GetRVA` 默认恒等，`NSO.cs` 没有覆写）。`.text mem_off` 只用于判断段归属，不参与地址运算。

| 目标 | 公式 |
|---|---|
| 运行时地址 | `模块基址 + RVA` |
| Ghidra（Adubbz Switch Loader） | `0x7100000000 + RVA` |
| dump.cs 的 `Offset:` 列 | 解压后 NSO 的文件偏移，不是磁盘文件偏移 |

合法 RVA 范围是 `[0, image_size)`，`image_size` 含 `.bss`（`nsotool.py info` 打印），`30-gen-offsets.py --image-size` 可据此校验。

## 风险

- **Build ID 绑定**：游戏每次更新所有 RVA 全变。启动时校验 Build ID，不匹配直接不装 hook。
- **Switch 2 不适用**：没有公开可用的 CFW。
- **联机封号**：装 CFW mod 后不要联网，用 90DNS 或飞行模式。

## 参考

| 项目 | 用途 |
|---|---|
| [Atmosphere-NX/Atmosphere](https://github.com/Atmosphere-NX/Atmosphere) | CFW 本体 |
| [shadowninja108/exlaunch](https://github.com/shadowninja108/exlaunch) | subsdk 注入框架与内联 hook |
| [Perfare/Il2CppDumper](https://github.com/Perfare/Il2CppDumper) | IL2CPP 符号还原 |
| [jakcron/nstool](https://github.com/jakcron/nstool) | NSP/XCI/NCA 处理 |
| `Silksong_HealthBar`（PC） | 血条外观与数值参考 |
| Completionist Map（PC，Yoraiz0r） | 收集物数据与判定规则来源 |

## 法律边界

只对自己的主机、自己的游戏做转储与修改。游戏转储、`prod.keys`、`global-metadata.dat`、`dump.cs`、
`script.json` 以及由它们生成的 `offsets.h` / `collectibles.gen.h` 都不入库，`.gitignore` 已覆盖，不要绕过。
BOSS 条贴图是作者自绘；面板文字贴图用 Noto Sans SC（SIL OFL）渲染。
