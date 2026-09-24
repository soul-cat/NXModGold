# 任务：把血条画到屏幕上（exl 路线）

## PC 参考实现：`XiaoHaiiOvO/Silksong_HealthBar`（★41，2026-09 仍在维护）

BepInEx 5 + Harmony，纯 Unity 托管代码：

| 部分 | 做法 |
|---|---|
| 数据 | Harmony `Prefix/Postfix` 挂 `HealthManager.TakeDamage(HitInstance)`，打前/打后快照算伤害；处理 `SendDamageTo` 重定向 |
| 普通敌人 | `AddComponent<EnemyHealthBar>` 到敌人 GameObject；`Canvas(RenderMode.WorldSpace)`，`LateUpdate` 里放到 `transform.position + up*offset`，缩放 0.01 |
| BOSS | `Canvas(ScreenSpaceOverlay)` + `CanvasScaler(ScaleWithScreenSize)`，屏幕固定，多 BOSS 自动排列 |
| 血条 | `Image.Type.Filled` 横向 `fillAmount = hp/max`，背景 `Image`，圆角 Sprite 缓存 |
| 文字 | `UnityEngine.UI.Text`，`"{hp}/{max}"`，低血量变白 |
| 伤害数字 | 对象池（上限 32）的 Canvas + Text，飘字 |
| 最大血量 | 不读 `initHp`，用 `max(打前, 打后)` 推断（我们已实测 `initHp` 可用） |

它只在玩家真正造成伤害后才建 UI，不扫描场景。

### PC 的 BOSS 血条（用户点名要的效果，默认值来自 `Plugin.cs`）

| 项 | 默认 |
|---|---|
| 触发 | 初始最大血量 > `HealthThreshold` 105 |
| 画布 | `ScreenSpaceOverlay`，`CanvasScaler` 参考分辨率 1920×1080，`sortingOrder 100` |
| 位置 | 屏幕**底部居中**（`BottomPosition=true`），距边 42；多个 BOSS 向上堆叠，间距 = 高 + 54 |
| 尺寸 | 900 × 25 |
| 颜色 | 填充 `#BEB8B8`（浅灰），背景 `#00000080`（半透明黑），填充内缩 2 px |
| 形状 | 默认矩形；可选圆角（半径 30） |
| 名称 | 条上方，字号 28，取 `EnemyDeathEffects.journalRecord.DisplayName`（猎人日志里的**本地化名字**），退化为 GameObject 名 |
| 数值 | `当前/最大`，字号 24，居中在条上 |
| 字体 | 游戏自带字体（`DamageTextManager.LoadFont`），所以和游戏 UI 风格一致 |

Switch 上的对应：屏幕固定，不需要世界坐标跟随，最适合作为第一个上屏的效果。
名称要多 hook/调用 `EnemyDeathEffects` → `journalRecord` → `DisplayName`。

## Switch（IL2CPP）上能用的同名 API（v1.0.3 dump 核对）

- uGUI：`Canvas`/`CanvasScaler`/`UnityEngine.UI.Image`/`UnityEngine.UI.Text`、TMP 都在
- IMGUI：`UnityEngine.GUI` 在；有 63 个 `OnGUI`（`PlayMakerGUI`、`GUIDrawer` 等）
- 坐标：`Camera.get_main` / `WorldToScreenPoint(Vector3)` / `Component.get_transform` /
  `Transform.get_position` —— 后两者正是 `HealthManager.Hit` 自己调用的函数（反汇编核对）

区别：我们不能新增 C# 类（没有托管代码），只能从原生代码调用已编译的方法。

## 渲染方案

| 方案 | 做法 | 优点 | 代价 |
|---|---|---|---|
| A. IMGUI | 在每帧的 `OnGUI` 里调 `GUI.DrawTexture(Texture2D.whiteTexture)` + `GUI.Label` | 不用创建任何对象；即时模式，逻辑与已有 `hud_layout.hpp` 相同 | 需要一个每帧被调用的 `OnGUI`；字符串要靠 `int.ToString` 等托管方法生成 |
| B. uGUI（照搬 PC） | 原生调用 `new GameObject`、`AddComponent(typeof(Canvas))`… | 视觉与 PC mod 一致 | 要从原生分配托管对象、取 `System.Type`，工作量大 |
| C. NVN + ImGui | hook 图形 API 自己画（d3hack 的做法） | 与 Unity 无关 | 图形集成最重 |

先走 A；A 走不通（没有可用的 OnGUI）再退 B。

## 步骤 → 验证

| # | 步骤 | 验证 |
|---|---|---|
| 1 | 探针：候选每帧回调计数 + 受击时原生调 Unity 算屏幕坐标 | 日志：哪个回调每帧触发；`pos ... screen (x, y)` 落在 `cam WxH` 范围内 |
| 2 | 在选定回调里画一个固定位置的矩形 | 游戏画面左上角出现色块 |
| 3 | **BOSS 血条**（照 PC 默认样式：底部居中 900×25、名称、`当前/最大`） | 打 BOSS 时底部出现血条，数值随 hp 变化 |
| 3b | 普通敌人屏幕固定血条 + 数值 | 打怪后血条随 hp 变化，淡出 |
| 4 | 世界坐标跟随 | 血条跟在怪物头顶 |
| 5 | 伤害数字飘字 | 每次命中飘出伤害值 |

## 进度

- 步骤 1：标题画面上 `CameraController.LateUpdate` 每帧触发（600 次 / 13 s）；
  两个 `OnGUI` 在标题画面未触发，待进游戏确认；受击坐标待打怪确认。
