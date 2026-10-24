# 开发说明

构建、架构与工具链。功能和使用见 [README](../README.md) 与 [usage.md](usage.md)；
当前状态、实机验证记录和踩过的坑见 [HANDOFF.md](../HANDOFF.md)。

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
  ├─ UIManager.UIGoToGameOptionsMenu 打开「选项 → 游戏」前：首次克隆插入七个开关行，之后刷新行上的值
  ├─ MenuButtonList.SetupActive      页面激活重排导航后，把新增行接进上下导航
  └─ MenuOptionHorizontal.UpdateSetting  左右切换后：是我们的行就改开关、写回 config.ini
```

所有 hook 体都在 Unity 主线程上跑，`HudModel` 等共享状态不加锁。
地址约定：运行时地址 = main 模块基址 + RVA，RVA 全部来自生成的 `exl/program/offsets.h`，代码里不硬编码。

## 目录

| 路径 | 用途 |
|---|---|
| `exl/program/main.cpp` | 入口：Build ID 校验（从 main `.rodata` 的 GNU build-id note 取），装 hook |
| `exl/program/hud_hooks.hpp` | TakeDamage / LateUpdate / OnGUI 三个 hook |
| `exl/program/hud_model.hpp` / `hud_phase_model.hpp` | 血条纯逻辑：敌人槽位（16）、飘字池（32）、布局、占位血量与分段 BOSS 段表；宿主机可测 |
| `exl/program/hud_boss_phases.hpp` | 从 BOSS 的 FSM `Phase Control` 读各段血量 |
| `exl/program/hud_theme_model.hpp` / `hud_theme.hpp` | 默认 / 皮肤两套调色板，启动时按模块表选主题 |
| `exl/program/hud_font.hpp` | HUD 文字换用游戏的中文字体（`GUIStyle.SetDefaultFont`，画完还原） |
| `exl/program/boss_rush*.hpp` | BOSS 挑战：BOSS 表（`boss_rush_table.hpp`）、菜单纯逻辑与标记账本（`_model`）、游戏调用（`_calls`）、4 号位存档拦截（`_save_guard`）、菜单文字与绘制 |
| `exl/program/fsm_lookup.hpp` | 按 GameObject + 名字找 PlayMaker 状态机 |
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
| `docs/` | 使用说明、共存方案、发版说明与研究记录（`docs/tasks/`） |

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
- 完整构建必须 `work/completionist/Completionist_Map-1.0.3.zip` 存在；增量构建不会重新评估
  `__has_include`，增删该头文件后先 `make -C exl clean`。

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
./tools/70-package-release.sh 1.0.9   # → work/release/SilksongHUD-v1.0.9.zip
```

皮肤共存包（需要原始皮肤包与 union npdm）：

```bash
./tools/40-patch-npdm.py patch --in work/dump/ExeFS/main.npdm --out work/release/main.npdm
./tools/85-repack-skin-coexist.py --skin-zip <原始皮肤包.zip> \
    --npdm work/release/main.npdm --out work/release/SilksongSkin-coexist.zip
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
| `test_hud_model` | 槽位复用与淘汰、隐藏延时、BOSS 条不超时、BOSS 门槛、占位血量、分段 BOSS（苍白之母实测序列）、布局、飘字进度与回收 |
| `test_hud_theme` | 主题选择、皮肤调色板、低血量 smoothstep 边界与混色 |
| `test_toggles` | R3 面板输入（按住、方向映射、按下沿、同时按、松开）、`config.ini` 格式化与解析 |
| `test_options_menu_model` | 原生选项页：从本地化键认出「开」是第几项、按游戏语言选标签、行键名、行距 / 缩放换算、纹章后备名判定、UTF-16 → ASCII 日志转换 |
| `test_map_timing` | 地图刷新耗时的批量汇总 |
| `test_map_refresh_plan` | 两个挂钩之间该建标记、完整刷新还是隐藏（含大地图与快速地图的调用顺序差异） |
| `test_boss_rush_model` | BOSS 挑战菜单：组合键开关、翻页、滚动窗口、固定项、BOSS 表完整性、4 号位存档路径判定、标记账本 |

工具自检：`./tools/nsotool.py selftest`、`./tools/40-patch-npdm.py selftest`、
`./tools/50-gen-collectibles.py selftest`、`./tools/60-gen-bar-textures.py selftest`。

### 5. 流水线一览（游戏更新后重跑）

```bash
./tools/05-unpack-container.sh --keys <prod.keys> <base.nsp> <update.nsz>
./tools/10-extract.sh work/dump
./tools/20-dump-il2cpp.sh
./tools/30-gen-offsets.py --dump work/il2cpp/dump.cs --script work/il2cpp/script.json \
    --main-bin work/main.bin --build-id "$(cat work/BUILD_ID)"
./tools/50-gen-collectibles.py
make -C exl
```

| 步骤 | 作用 | 要点 |
|---|---|---|
| `05-unpack-container.sh` | NSP/NSZ/XCI → `work/dump/`（ExeFS 的 `main`、`main.npdm`，RomFS 的 `global-metadata.dat`） | RomFS 用 `tools/ncaext`（LibHac），见 HANDOFF |
| `10-extract.sh` | 暂存文件、写 `work/BUILD_ID`、解压 `work/main.bin` | 用 `nsotool.py` |
| `20-dump-il2cpp.sh` | Il2CppDumper → `work/il2cpp/{dump.cs,script.json}` | 自动打 `il2cppdumper-unity6.patch`，自动调 `find-il2cpp-registrations.py` |
| `30-gen-offsets.py` | → `exl/program/offsets.h` | 符号表在 `tools/targets.json`；共享泛型实例 / metadata 槽 / TypeInfo 槽由 `offsets_script.py` 从 `script.json` 取 |
| `40-patch-npdm.py` | `make -C exl` 自动调用，原版 npdm 补 SVC 与 fs 权限 | 见 HANDOFF「exlaunch 坑」 |
| `50-gen-collectibles.py` | Completionist Map 包 → `collectibles.gen.h`（571 条） | 数据无 license，不入库 |
| `60-gen-bar-textures.py` | `exl/assets/**/*.png` → `bar_textures.gen.h` | `make -C exl` 自动调用 |
| `61-render-menu-text.py` | 面板中文 → `exl/assets/menu/*.png` | 仅改文字时手动跑，需要 Pillow + Noto Sans SC Bold |

**每次重新生成偏移都要反汇编抽查几个 setter**（名字对不代表地址对），方法与具体地址见 HANDOFF。

## 不变量

完整列表见 [HANDOFF.md](../HANDOFF.md) 的「不变量」一节，最关键的四条：

1. **Build ID 不匹配就什么都不做。**
2. **所有 RVA 只来自生成的 `offsets.h`。**
3. **`exl_main` 里不碰 Unity**（IL2CPP 尚未初始化），所有 Unity 调用都在 hook 体里。
4. **不跨帧持有未钉住的托管指针。**

## 参考

| 项目 | 用途 |
|---|---|
| [Atmosphere-NX/Atmosphere](https://github.com/Atmosphere-NX/Atmosphere) | CFW 本体 |
| [shadowninja108/exlaunch](https://github.com/shadowninja108/exlaunch) | subsdk 注入框架与内联 hook |
| [Perfare/Il2CppDumper](https://github.com/Perfare/Il2CppDumper) | IL2CPP 符号还原 |
| [jakcron/nstool](https://github.com/jakcron/nstool) | NSP/XCI/NCA 处理 |
| `Silksong_HealthBar`（PC） | 血条外观与数值参考 |
| Completionist Map（PC，Yoraiz0r） | 收集物数据与判定规则来源 |
