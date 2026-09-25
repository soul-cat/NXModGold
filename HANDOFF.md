# 接手文档

给下一个接手本项目的人。先读这份，再读 `README.md`（功能、构建、安装、使用）。研究细节在 `docs/tasks/`。

---

## 一句话

《空洞骑士：丝之歌》Switch 版（v1.0.3，Build ID `FC9EA4CCC955D579`）的 exlaunch mod：
`subsdk9` + 修补过的 `main.npdm` 经 Atmosphère exefs 注入，在游戏进程内 hook IL2CPP 方法，
用 Unity IMGUI 画敌人血条 / BOSS 条 / 伤害数字，克隆游戏自己的地图标记画收集物。
这是唯一的路线；早期的 SaltyNX 插件 + Tesla overlay 路线已删除。

---

## 当前状态

### 实机已验证

| 项 | 备注 |
|---|---|
| 加载、Build ID 校验、hook 安装 | `SD:/config/sshud/log.txt` 可见 `build id ... -> match` 与 hook 安装行 |
| 敌人血条 + 伤害数字 | 跟随敌人头顶；左上角调试标记已在确认绘制后删掉（`7d3a054`） |
| 地图收集物标记（快速地图 + 大地图） | 游戏自带图标，区域地图未买时隐藏（`47d4074`） |
| 已收集物品刷新 | 同一房间刚捡的物品开图即消失（刷新前调 `GameManager.SaveLevelState`，`4a652f1`） |
| R3 开关面板 | 中文贴图文字，设置写回 `config.ini` |
| 原生选项页开关 | 克隆「选项 → 游戏」的开/关行插四行；实机日志：6 个原生行、布局组自动排版、缺字 0、导航接在 `BackerOption` 与 `DefaultsButton` 之间（`ff49964`）；第五行 R3 快捷面板开关；第六行送货不损坏（hook `DeliveryQuestItem.TakeHitForItem` / `BreakAllInternal`，实机日志：挨打、危险地形重生、计时掉耐久均被拦下，钟兽 / 电梯可正常乘坐） |
| 原生跳蚤图钉去重 | 实机日志：6 组 29 个图钉，名字即跳蚤存档键，`parentScene` 全空；按名字匹配，重复的 6 个跳蚤标记隐藏（`e8337ab`） |
| 存档导入 / 导出 | 导入 `user1..4.dat`（含备份与 `.done` 改名）；导出整个 `save:/` |
| 开图卡顿修复 | 原来每次开图约 330 ms 卡顿；场景查询改为跨刷新缓存（`c7dded7`），托管字符串钉住复用、日志只在变化时写（`1b24c4d`） |

### 待实机确认

- **BOSS 条贴图外观**（`1760685`）：外框 / 填充贴图的比例、内缩量（`BOSS_FILL_INSET_X_RATIO` / `_Y_RATIO`）和尖角效果还没在实机上看过。
  贴图建不出来时会自动退回纯色条（`hud_draw.hpp:drawBoss`）。

### 已知限制

- **BOSS 判定只看血量**：最大血量 > 105 就画 BOSS 条（与 PC 版一致），大体型普通敌人也会被当成 BOSS。
  `recordDamage` 每次命中都记 `size` / `big` / `kills`（`enemySize`、`bigEnemyDeath`、猎人日志击杀数）到日志，就是为了调研更准的判定。
- **BOSS 扫描窗口**：`DisplayBossTitle` 之后 5 秒内每帧扫激活敌人；更晚才激活且不出标题的 BOSS 要等第一刀才出条。
- **收集物覆盖不全**：生成器只收能用 4 种 `CheckKind`（`PersistentBool` / `PersistentIntAtLeast` / `GeoRockDepleted` / `PlayerDataBool`）表达的条目，
  需要运行时物品状态判定的（部分工具、典藏、纪念品等）被跳过，跳过原因由 `tools/collectibles_sources.py` 输出。
- **图标可能缺失**：图标靠 `Resources.FindObjectsOfTypeAll(Sprite)` 按名字找，只能找到已载入的资源；最多重试 5 次开图，
  之后仍缺的退回按分类染色的圆点，缺失的贴图名会写进日志。
- 地图标记开关在下次开图时生效（标记只在开图时刷新）。

---

## 流水线（游戏更新后重跑）

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
| `05-unpack-container.sh` | NSP/NSZ/XCI → `work/dump/`（ExeFS 的 `main`、`main.npdm`，RomFS 的 `global-metadata.dat`） | RomFS 用 `tools/ncaext`（LibHac），见下文「转储坑」 |
| `10-extract.sh` | 暂存文件、写 `work/BUILD_ID`、解压 `work/main.bin` | 用 `nsotool.py` |
| `20-dump-il2cpp.sh` | Il2CppDumper → `work/il2cpp/{dump.cs,script.json}` | 自动打 `il2cppdumper-unity6.patch`，自动调 `find-il2cpp-registrations.py` |
| `30-gen-offsets.py` | → `exl/program/offsets.h` | 符号表在 `tools/targets.json`；共享泛型实例 / metadata 槽 / TypeInfo 槽由 `offsets_script.py` 从 `script.json` 取 |
| `40-patch-npdm.py` | `make -C exl` 自动调用，原版 npdm 补 SVC 与 fs 权限 | 见下文「exlaunch 坑」 |
| `50-gen-collectibles.py` | Completionist Map 包 → `collectibles.gen.h`（571 条） | 数据无 license，不入库 |
| `60-gen-bar-textures.py` | `exl/assets/**/*.png` → `bar_textures.gen.h` | `make -C exl` 自动调用 |
| `61-render-menu-text.py` | 面板中文 → `exl/assets/menu/*.png` | 仅改文字时手动跑，需要 Pillow + Noto Sans SC Bold |

**每次重新生成偏移都要反汇编抽查几个 setter**（名字对不代表地址对）：例如 `set_EnemyType` 应是
`str w1, [x0,#0xB8]`、`get/set_IsInvincible` 应是 `ldrb/strb [x0,#0x168]`，与 dump 的字段偏移一致；
`TakeDamage` 读写 `[x1+0x44]`（`DamageDealt`）与 `[x0+0xA8]`（`hp`）。

---

## 不变量

1. **Build ID 不匹配就什么都不做。** `main.cpp:verifyBuildId` 从 main `.rodata` 尾部找 GNU build-id note，
   前 8 字节与 `SS_BUILD_ID_STR` 比对，不一致直接 return，不装 hook。
2. **所有 RVA 只来自生成的 `offsets.h`。** 代码里不写任何数字地址。
3. **`exl_main` 里不碰 Unity。** rtld 在 main 入口之前初始化 subsdk，此时 IL2CPP 未初始化；所有 Unity 调用都在 hook 体里（主线程）。
4. **不跨帧持有未钉住的托管指针。** 本模块的静态区 GC 扫不到。需要跨帧的（BOSS / 面板贴图、地图查询用的字符串）
   用 `GCHandle.Alloc` 钉住；贴图还要设 `hideFlags = DontUnloadUnusedAsset`，否则切场景会被 `UnloadUnusedAssets` 卸掉。
   地图的 `IconSet` 含托管指针，必须放栈上。
5. **IMGUI 全局状态进出还原。** `ImguiStateGuard` 保存 / 还原 `GUI.color`、`contentColor`、label 字号与对齐，不影响游戏自己的 OnGUI。
6. **测试红了先怀疑代码，不要改测试。**

---

## 会浪费时间的坑

### 地址与 IL2CPP 调用

- **运行时地址 = 模块基址 + RVA，不减 `.text mem_off`。** Il2CppDumper 对 NSO 的 `GetRVA` 是恒等函数（`NSO.cs` 没覆写）。
- **IL2CPP 方法原生签名末尾多一个 `const MethodInfo*`。** hook 回调必须原样转发（`TakeDamage` 是 x0 / x1 / x2）。
  非泛型方法传 `nullptr` 即可；**共享泛型实现**（如 `PersistentItemDataCollection<T>.TryGetValue`）必须传 metadata 槽里的
  `MethodInfo*`，槽要先用 `il2cpp_codegen_initialize_runtime_metadata` 初始化（照抄游戏自己的调用方式，`map_collected.hpp`）。
- **`HitInstance` 是大号值类型，经隐藏指针在 x1 传**，不是寄存器传参。
- **最大血量字段是 `initHp`**（没有 `maxHP`）；实际取 `max(initHp, 打前 hp)`。
- **读静态字段**：TypeInfo 槽 → `Il2CppClass*`，`klass+0xE4` 非 0 表示静态构造完成，`klass+0xB8` 是 static_fields（照游戏 `HealthManager.OnEnable` 反汇编）。
- **`Vector3` / `Color` / `Rect` 是 float HFA**，AAPCS64 下经 s 寄存器传，可按值传。

### exlaunch / 加载

- **SDK 20+ 的 rtld 与 exlaunch 模块头不兼容**（实机同样会崩）：rtld 只把首字为 `0` / `0xEA000000` / `0x14000002` 的模块当旧式头，
  否则把 +8 处的字当扩展头偏移。exlaunch 首字是 `b entrypoint`，+8 是 nop，rtld 读到 `基址+0xD503201F` 直接非法访问。
  `exl/patches/rtld-sdk20-header.patch` 把首字改成 `b +8`，构建时自动打到 stage 上。
- **`main.npdm` 必须基于游戏原版**：exlaunch 要 `svcMapProcessMemory` 等 4 个零售游戏没有的 SVC。`40-patch-npdm.py` 只补 ACID/ACI0 的 KAC，
  并把 fs 权限放开到 `0xffffffffffffffff`（原版只有 Debug 位，挂不了 SD），其余字节原样。不要用 exlaunch 模板 json 生成（别的游戏的栈大小 / 服务表）。
- **GCC 16** 对 exlaunch 的 `hook_impl.cpp` 报 `-Wnon-c-typedef-for-linkage`（它开了 `-Werror`），`exl/config.mk` 已加 `-Wno-`。
- **必须 `-fno-threadsafe-statics`**（`exl/config.mk` 已加）：带构造函数的函数内 `static` 默认由 `__cxa_guard_acquire` 加锁，它调 newlib 的
  `pthread_once` / `pthread_mutex_lock`，本模块没有 libnx 线程原语，实机第一次进入就卡死（曾让开大地图卡死）。改完可用 `nm` 确认没有 `__cxa_guard_acquire`。
- `exl/Makefile` 每次构建把 `vendor/exlaunch` 同步到 `exl/build/sshud/`，不改 vendor；不往仓库外写文件，部署手动拷。

### 绘制

- **只在 Repaint 事件里画**：`DrawTexture` / `Label` 在其它 IMGUI 事件里不出像素。绘制入口是 `PlayMakerGUI.OnGUI`，
  `CameraController.LateUpdate` 每帧确保 PlayMakerGUI 存在。
- **IMGUI 默认字体在 Switch 上没有中文字形**，面板文字只能离线渲染成白色贴图再用 `GUI.color` 染色。
- **`DrawTextureWithTexCoords` 被裁掉了**，所以 BOSS 条贴图离线切成左 / 中 / 右三张（三宫格），两端等比，中段拉伸。
- Unity 原始纹理数据自下而上存行，生成器已提前上下翻转。

### 地图标记

- **两处都要刷新**：大地图先 `SetupMapMarkers` 再 `EnableUnlockedAreas`，快速地图相反；区域激活状态只有在后者之后才准。
  只挂前者时实机日志显示大地图 570 个全被判成「无区域地图」。
- **刷新前先调 `GameManager.SaveLevelState`**：游戏平时只在离开房间时把房间内物品状态写进 SceneData，不调的话同一房间刚捡的东西开图还在。
- **克隆的标记要删掉 `InvMarker` / `InvMarkerCollide`**，否则标记菜单会把它当玩家标记处理。
- **开图卡顿**：按名字查 322 个场景（`GetMapZoneFromSceneName` + `GetSceneInfo`）每次刷新约 160 ms，每次开图刷新两遍。
  现在 `SceneCache` 跨刷新保留，每次只重读 `IsMapped` 与 `activeInHierarchy`；换了 `GameMap` 或标记重建时整体作废。
  SD 日志每行都是开 / 写 / flush / 关，本身也会卡，所以统计只在结果变化时写，耗时每 8 次刷新汇总一行。

### SD 与存档

- **`exl_main` 时游戏的 fs 还没就绪**：日志先缓冲在内存（16 KB），首个 `LateUpdate` 里 `MountSdCardForDebug` 后再写。挂载失败只在 svc 日志报一行 `sd log disabled`。
- **exlaunch 头文件里的 `nn::fs::DirectoryEntry` 多了 8 字节**，真实布局是 0x310（同 libnx `FsDirectoryEntry`），`save_export.cpp` 自带 `RawDirectoryEntry`。
- 存档挂载名 `save`、文件名 `user{N}.dat` 来自游戏自己的字符串常量（`SwitchPlatform`）。写存档后必须 `CommitSaveData("save")`。
- 执行顺序固定为「日志 → 导出 → 导入」，导出的永远是导入前的原样存档。

### 转储（Unity 6，全部已解决）

1. **NCA 压缩分区（固件 16+）**：nstool 不支持，报 hash 校验失败。用 `tools/ncaext`（LibHac）。LibHac 的 ExternalKeySet 存**加密** titlekey。
2. **更新档 RomFS 是增量**：必须叠在 base 上重建，且方向是 `baseN.OpenFileSystemWithPatch(updN)`。曾经写反，
   解出的是 v1.0.0 的 `global-metadata.dat`（16,261,740 字节；v1.0.3 为 17,153,796 字节），名字全对但所有 RVA 错位。
3. **Il2CppDumper 自动搜索失败**：Unity 6 的 metadata image 数（78）与二进制 codeGenModules 数（80）不等。
   `find-il2cpp-registrations.py` 定位两个 registration 走手动模式，`20-dump-il2cpp.sh` 已自动化。
4. **786 个 codegen 附加类型**（klassIndex ≥ typeDefs.Length）：`il2cppdumper-unity6.patch` 做逐类型容错与占位。

其它：Il2CppDumper 运行需 `DOTNET_ROLL_FORWARD=LatestMajor`、输出目录必须先存在、`config.json` 的 `RequireAnyKey` 要设 `false`；
macOS 自带 bash 3.2 会把全角括号当变量名字符（写 `${var}（`）；nstool 解不出分区时也返回 0，只能看产物。

---

## 模拟器（Ryujinx）

同一份 `subsdk9` + `main.npdm` 可以在 Ryujinx 加载（放到 `<Atmosphere Mods 目录>/sshud/exefs/`），但现在的测试以实机为主：
模拟器在下列设置下很慢，首次进新地图要现编译大量代码。

- **必须关 Hypervisor**（`use_hypervisor: false`）：Unity GC 跨线程 `svcGetThreadContext3` 在 Apple Hypervisor 上返回 BadArgument 直接崩（与 mod 无关）。
- **必须用 `memory_manager_mode: SoftwarePageTable`**：HostMapped 模式下 `svcMapProcessMemory` 不是真别名，exlaunch 的 `RwPages` 断言失败、不装 hook。
- 只玩不测 mod 时可改回 `HostMappedUnsafe`（hook 不装，但不崩）。
- 数据目录 `datas/portable/`（`tools/run-ryujinx.sh` 或直接双击 `datas/Ryujinx.app`），SD 日志在 `datas/portable/sdcard/config/sshud/log.txt`。
- 本地 Ryujinx（Ryubing 1.3.3）是从源码自建的：`vendor/ryubing-build/output_arm64/Ryujinx.app`，源码与依赖以 nixpkgs 的哈希为锚逐一校验。官方发布可用时优先用官方。

---

## 调试

- 日志：`SD:/config/sshud/log.txt`，同时走 `svcOutputDebugString`。日志文本用英文（Ryujinx 按 ASCII 解码）。
- 关键行：`build id ... -> match`、`hooks installed`、`map hooks installed: 571 collectibles`、`PlayMakerGUI ready`、
  `first OnGUI, drawing enabled`、`hit hm=... dmg ...`、`boss title shown`、`map: created=... visible=...`、`map profile us: ...`（前 6 次刷新的分段耗时）、`config: ...`、`save import/export ...`。
- 实机必须装 v1.0.3 更新档，否则日志是 `MISMATCH, hooks not installed`。
- **不要联网**（封号）。

---

## 其他机器接手需要的材料（都不在版本库里）

1. `datas/`：base NSP、更新档 NSZ、`prod.keys`（自己主机导出）。
2. devkitPro（`switch-dev`）、.NET 8+ SDK（`ncaext` 与 Il2CppDumper）。
3. `vendor/`：`exlaunch`（`git clone --depth 1 https://github.com/shadowninja108/exlaunch.git vendor/exlaunch`）、
   `Il2CppDumper`（`00-setup.sh` 会拉）、`nstool`（05 脚本自动下载）。
4. `work/completionist/Completionist_Map-1.0.3.zip`（自己下载）；改面板文字时还需 `work/fonts/NotoSansSC-Bold.otf`。

---

## 下一步

1. 实机确认 BOSS 条贴图外观，必要时调 `BOSS_BAR_HEIGHT` 与内缩比例。
2. 用日志里的 `size` / `big` / `kills` 数据改进 BOSS 判定，减少大体型普通敌人误判。
3. 补上 4 种 `CheckKind` 表达不了的收集物（需要运行时查 ToolItem / Materium / Memento 等状态）。
4. 游戏更新时：重跑流水线 → setter 反汇编抽查 → 实机确认 `build id ... -> match`。

---

## 研究资料

- `docs/tasks/hud-draw.md`：PC 血条参考与 Switch 绘制方案
- `docs/tasks/research-collectible-map.md`：地图收集物（Completionist Map 数据与做法）
- `docs/tasks/research-il2cpp.md`：RVA 语义、Silksong 类结构、工具链
- `docs/tasks/research-overlay.md`、`docs/tasks/hud-v1.md`：已弃用的 Tesla overlay 路线，仅作历史记录
