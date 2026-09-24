# 调研：收集物地图标注（v2）

目标：在游戏自带地图上显示收集物标记，效果对齐游戏的跳蚤标记——没拿到就显示，拿到就消失。
约束：exlaunch `subsdk9`，只能 hook / 调用已编译的 IL2CPP 方法，不能新增 C# 类。

证据来源：`work/il2cpp/dump.cs`（v1.0.3，行号为 dump.cs 行号）、`work/main.bin` 反汇编
（`aarch64-none-elf-objdump`，加上 script.json 符号标注）、PC mod 源码（见第 2 节 URL）。
没有资源文件（RomFS 已清理），所以**所有"某个 prefab 上挂了什么组件"的结论都是 未验证**，
只有代码逻辑是反汇编核实过的。

---

## 1. 游戏自己怎么做条件地图标记

### 1.1 相关类（dump.cs）

| 类 | 行 | 作用 |
|---|---|---|
| `GameMap` | 131610 | 整张地图（MonoBehaviour），prefab 由 `InventoryMapManager.EnsureGameMapSpawned`(0x27D5270) 惰性实例化 |
| `GameMap.ZoneInfo` / `ParentInfo` | 131444 / 131356 | 每个 `MapZone` 一组"父节点"，父节点下是一张张场景地图 |
| `GameMapScene` | 132074 | 单个场景的地图块（SpriteRenderer），GameObject 名 = 场景名 |
| `MapPin` | 140804 | 地图标记的显隐状态（全局 Pins/Key 开关 + 所在场景是否已绘制） |
| `MapPinConditional` | 140926 | **条件显隐**：PlayerData 测试 + 场景持久布尔 |
| `GameMapPinLayout` | 131996 | 同一位置多个 pin 的自动排布 |
| `CaravanTroupeHunter` | 24220 | 卖跳蚤地图标记的商店，按区域写 `hasPinFlea<区域>` |
| `QuestMapMarker : MapMarkerArrow` | 142546 | 任务标记（绑定 `BasicQuestBase`/`CollectableItem`），超出视口时显示箭头 |
| `MapMarkerMenu` | 140370 | 玩家手动放置的 5 种标记（`PlayerData.placedMarkers` 0x328） |

### 1.2 关键字段（偏移）

`MapPinConditional`（行 140926）：

| 字段 | 偏移 | 说明 |
|---|---|---|
| `visibleCondition` : `MapPinConditional.Condition` | 0x20 | 可见条件 |
| `materialCondition` : `Condition` | 0x28 | 满足时换材质 |
| `material` / `spriteRenderer` / `initialMaterial` | 0x30 / 0x38 / 0x40 | |

`MapPinConditional.Condition`（行 140907）：`PlayerDataTest PlayerDataTest` @0x10，`PersistentBoolMatch PersistentBool` @0x18（内联结构体）
`PersistentBoolMatch`（行 140897，结构体内偏移）：`string Id` +0x0，`string SceneName` +0x8，`bool ExpectedValue` +0x10
→ 在 Condition 对象里分别是 0x18 / 0x20 / 0x28。

`PlayerDataTest`（行 40209）：`TestGroup[] TestGroups` @0x18（组之间 OR，组内 `Test[]` AND）；
`Test`：`Type`+0x0，`string FieldName`+0x8，`bool BoolValue`+0x10，`NumType`+0x14，`IntValue`+0x18 …

`MapPin`（行 140804）：`activeCondition`@0x20（None / CurrentMapZone），`hideIfOtherActive`@0x28，
`parentScene`@0x30，`gameMap`@0x38，`isActive`@0x40，`pinLayout`@0x48；静态 `_activePins`。

`GameMap`（行 131610）：`mapMarkerTemplates`@0xD0，`mapZoneInfo`@0xD8，**`fleaPinParents` : `Transform[]`@0xE0**（按 `CaravanTroupeHunter.PinGroups` 索引），
`mainQuestPins`@0xE8，`currentScenePos`@0x5C，`currentSceneSize`@0x64，`currentSceneObj`@0x70，`currentScene`@0x78，
`markerParent`@0x200，`corpseSceneMapZone`@0x4C。

`PlayerData`：`hasPinFleaMarrowlands..hasPinFleaMucklands` 0x317–0x31C（行 90053–90058），
`SavedFlea_*` 27 个 bool 0x810–0x82A（行 90908–90934），`mapAllRooms` 0x2E1，`Has<区域>Map` 0x2E4…，
`scenesMapped : HashSet<string>` 0x2C8，`HeroCorpseScene` 0x100 / `HeroDeathScenePos` 0x108 / **`HeroDeathSceneSize` 0x110**。

### 1.3 显隐判定（反汇编核实）

**`MapPinConditional.OnEnable` @0x2811EC0**：

```
if (!visibleCondition.IsFulfilled) gameObject.SetActive(false)   // icall GameObject::SetActive_Injected
else spriteRenderer.sharedMaterial = materialCondition.IsFulfilled ? material : initialMaterial
```

**`Condition.get_IsFulfilled` @0x280AD40**：

```
if (!PlayerDataTest.IsFulfilled) return false;                      // 0x2279E30
if (PersistentBool.Id 为空 || PersistentBool.SceneName 为空) return true;
found = SceneData.instance.persistentBools.TryGetValue(SceneName, Id, out data)   // 0x3C14A70
v = found ? data.Value(+0x21) : false
return v == ExpectedValue
```

所以"没拿到才显示"只要写成 `PersistentBool = {SceneName, Id, ExpectedValue=false}`，
或者 `PlayerDataTest: <bool字段> == false`。

**`MapPin`**：`Awake`(0x2810A10) → `AddPin` 进静态列表；`Start`(0x2810C40) → `ApplyState(CurrentState)`；
`ApplyState`(0x2810850) → `gameObject.SetActive(state != None && CanBeActive())`；
`CanBeActive`(0x28111E0) 读 `hideIfOtherActive`、`GetComponentInParent<GameMapScene>` 的 `IsMapped`/`IsVisited`、
`activeCondition==CurrentMapZone` 时比 `GameMap.GetMapZoneForScene`。`CycleState`/`ToggleQuickMapView` 响应玩家的
"显示标记/图例"切换（PlayerPrefs 键 `MapPinVisibilityState`）。
→ `MapPin` 负责"场景已绘制 + 全局开关"，`MapPinConditional` 负责"条件"，两者串联：
`ApplyState` 把 GO 打开 → 触发 `MapPinConditional.OnEnable` → 条件不满足再关掉。

**跳蚤标记**：

- `CaravanTroupeHunter.PdBools`（.cctor 0x2C54CA0）把 `PinGroups{Marrowlands,Midlands,Blastedlands,Citadel,Peaklands,Mucklands}`
  映射到 `hasPinFlea<区域>` 字符串；`OnPurchasedItem`(0x2C54880) 用 `VariableExtensions.SetVariable<bool>` 置 true。
- `GameMap.HasRemainingPinFor(group)` @0x2782980：遍历 `fleaPinParents[group]` 的子节点，
  `PlayerData.GetVariable<bool>(child.gameObject.name)`，有一个为 false 就返回 true。
  **即跳蚤 pin 子节点的 GameObject 名就是 `SavedFlea_*` 字段名**（代码核实），商店据此只卖还有剩余跳蚤的区域。
- 子节点自身怎么隐藏：最可能是挂 `MapPinConditional`（`PlayerDataTest: SavedFlea_X == false`）——未验证（需要看 prefab）。
- 父节点怎么打开：`GameMap.ParentInfo.PlayerDataBool`(0x20) + `BoundsAddedByPinGroup`(0x28)；
  `ParentInfo.get_IsUnlocked`(0x278C040) = `mapAllRooms || PlayerData.GetVariable<bool>(PlayerDataBool)`，
  `CheckActivation`(0x278C780) 据此 SetActive。跳蚤父节点的 PlayerDataBool 是 `hasPinFlea*`——未验证（资源数据）。

### 1.4 pin 住在哪里、何时重新评估

- 层级：`GameMap` → `mapZoneInfo[zone].Parents[i].Parent`（区域根）→ 子节点 = 场景地图（名字 = 场景名，挂 `GameMapScene`）→ pin。
  `GetSceneInfo`(0x2787710) 与 `GetMapZoneFromSceneName`(0x277FA70) 都是按这个层级逐个比子节点名字（反汇编核实）。
  `MapPin.CanBeActive` 用 `GetComponentInParent<GameMapScene>`，说明普通 pin 是场景地图的子节点。
- 整个 `GameMap` 是 prefab（`InventoryMapManager.gameMapPrefab`@0xF0，实例放在 `gameMapParent`@0xF8），
  **pin 都是 prefab 里预先摆好的**，不是运行时生成。唯一运行时生成的是玩家手动标记：
  `GameMap.OnAwake`(0x277D9E0) 里 `Object.Instantiate(template, markerParent, false)`(0x5FF5F10)。
- 刷新时机（xref 核实）：
  - `GameMap.SetupMap(bool pinsOnly)` @0x2780F50 调用者：`InventoryMapManager.EnsureGameMapSpawned`、
    `GameManager.UpdateGameMap`、`GameManager.UpdateGameMapPins`、`CustomSceneManager.AddSceneMapped`、`HeroController.SetConfigGroup`。
    它做 `ParentInfo.Validate/CheckActivation`、`GameMapScene.SetMapped/SetNotMapped`、`GameMapPinLayout.Evaluate`。
  - 打开地图：`InventoryMapManager.ZoomIn` → `GameMap.WorldMap`(0x2782EE0)；快速地图 → `GameMap.TryOpenQuickMap`(0x2786DB0)；
    两者都调 **`GameMap.SetupMapMarkers`(0x2782FC0)**（放置/移除手动标记也会调）。
  - 条件 pin 在**每次 GO 由非激活变激活时**（`OnEnable`）重新判定。区域根由 `EnableUnlockedAreas`/`DisableAllAreas` 开关，
    所以每次开地图大概率会触发——未验证（需实机日志确认 OnEnable 频率）。

### 1.5 场景坐标 → 地图坐标（反汇编核实）

`GameMap.GetMapPosition(Vector2 posInScene, GameMapScene scene, GameObject sceneObj, Vector2 scenePos, Vector2 sceneSize)` @0x2787ED0：

```
b      = scene.BoundsSprite.bounds            // icall Sprite::get_bounds_Injected
spr    = 2*b.extents * scene.transform.localScale
result = scenePos - spr/2 + (posInScene / sceneSize) * spr      // GameMap 局部坐标
       // scene/sprite 为 null 时返回 (-1000,-1000)
```

- `scenePos` 由 `GetSceneInfo` 给出 = 场景地图 localPosition + 区域根 localPosition（相对 GameMap）。
- `sceneSize` = 该场景 `tk2dTileMap.width/height`（`GameMap.LevelReady` 里 `stur d0,[x19,#0x64]`，
  来源 `gm.tilemap`@0x138 的 `width`@0x44/`height`@0x48，转 float）。
- 指南针：`GetCompassPositionLocalBounds`(0x2787D90) = `GetMapPosition(hero.position, currentScene…, currentScenePos, currentSceneSize)`。
- **尸体标记**是"任意场景 + 坐标"的现成先例：`GetCorpsePosition`(0x2787CA0) =
  `GetSceneInfo(PD.HeroCorpseScene, corpseSceneMapZone)` + `GetMapPosition(PD.HeroDeathScenePos, …, PD.HeroDeathSceneSize)`。
  游戏为此专门把**场景尺寸存进存档**——说明离开该场景后游戏自己也拿不到场景尺寸。
  → **我们的数据表必须带每个场景的尺寸**（或者直接存归一化坐标 `x/width, y/height`）。

---

## 2. PC mod

### 2.1 有哪些

| Mod | 源码 | 说明 |
|---|---|---|
| **Completionist Map**（Yoraiz0r） [Thunderstore](https://thunderstore.io/c/hollow-knight-silksong/p/Yoraiz0r/Completionist_Map/) / [Nexus #675](https://www.nexusmods.com/hollowknightsilksong/mods/675) | **无公开仓库**；包里带 `CompletionistMapMod.dll` + `.pdb` + 18 个 JSON。用 ilspycmd 反编译核对（只在 scratchpad 里） | v1.0.3，2025-10-25 更新；BepInPlugin `yorai.completionistmap`。**包内没有 license 声明** |
| CollectionPin（ReishyouSose） [GitHub](https://github.com/ReishyouSose/CollectionPin) / Nexus #471 | 开源 | 手工摆的 219 个 pin，简单 |
| Silksong.ScreenshotMarker（DemoJameson） | 开源 | "往地图上加 pin"最干净的例子 |

### 2.2 Completionist Map 的做法（反编译源码核实）

**(a) 数据**：全部离线从 asset bundle 抽取（README：AssetsTools.NET + FSMExpress），运行时不扫场景。
`ModMapData.LoadMapDirect()` 后台线程读 JSON，再加大量硬编码修正。
每条记录 = 场景路径 + 从对象到场景根的 transform 链；世界坐标 = **各级 localPosition 直接相加**（忽略旋转缩放，`ExtensionMethods.WorldPosition`）。

真实条目（`persistent_bools.json`，节选）：

```json
{"Id":"Heart Piece","Details":"Behind Wall Control",
 "Hierarchy":{"CleanScenePath":"Crawl_02",
  "Transforms":[{"LocalPosition":{"X":22.23158,"Y":4.839,"Z":0},
   "Controls":{"Name":"Heart Piece","DisableIfTrue":"","Persistent":true,"PlayerDataTests":[]}}]}}
```

`persistent_scenes.json`：549 个场景的 tilemap 尺寸，例如 `Abandoned_town {Width:370, Height:60}`、`Crawl_02 {Width:30, Height:150}`。
规模（本地统计）：`persistent_bools` 2285 条（其中 `Heart Piece` 14、`Silk Spool` 13、`Collectable Item Pickup` 129、
`…Tool Metal` 5、`moss_berry_fruit` 4、`Silk Grub Large Cocoon` 10）、`item_locations` 232 条
（sheet：UI 149 / Tools 36 / CollectableRelic 19 / Quests 19 …）、`rosary_caches` 190、`geo_rocks` 33、`transition_points` 1597；共约 57 MB，36 个分类。

**(b) 已收集判定**（`PersistentBoolItemMapIcon.CoordinateIsClearedInternal`，按顺序）：
1. `SceneData.instance.persistentBools.TryGetValue(CleanScenePath, Id)` 且值为 true（Id = GameObject 名，如 `("Crawl_02","Heart Piece")`）
2. 自定义条件 / 完成用 PlayerData bool / `FullQuest.IsCompleted`
3. 按物品类型：`CollectableItemMemento.IsListedInCollection`、`CollectableItemBasic.uniqueCollectBool`、`MateriumItem.IsCollected`、
   `CollectableRelicManager.GetRelicData(r).IsCollected`、`PlayerDataCollectable.linkedPDBool`、`ToolItem.IsUnlocked`、`ToolCrest.IsUnlocked`
4. transform 上的 `DisableIfTrue/DisableIfFalse` PlayerData bool

其它：念珠缓存 `persistentInts >= CountToReach`、geo 石 `SceneData.geoRocks <= 0`。
显示范围受 `PlayerData.scenesMapped`（或 `scenesVisited`）限制。

**(c) 渲染**（`MapPatches.cs` / `MapMarkerRenderer.cs` / `MapUtils.cs`）：
- Harmony postfix `GameMap.Start` 缓存地图；另 patch `GetCurrentBounds`、`EnableUnlockedAreas`、`TryOpenQuickMap`、`PositionCompassAndCorpse`、`Update`。
- **克隆玩家手动标记模板** `map.mapMarkerTemplates[0]`，删掉 `InvMarkerCollide`/`InvMarker` 和第 0 个子节点，放大 ×2，
  换成白色 Sprite 按分类染色；父节点 = `map.spawnedMapMarkers[0,0].transform.parent`（即 `markerParent`）。
- 定位 `MapUtils.TryGetPosition`：

```csharp
MapCache val = mapCache[0];                                   // map.mapCaches[cleanScene]
MapZone z = map.GetMapZoneFromSceneName(val.sceneName);
map.GetSceneInfo(val.sceneName, z, ref scene, ref sceneObj, ref scenePos);
mapPosition = map.GetMapPosition(coord.WorldPosition(), scene, sceneObj, scenePos, mappedScene.Size);
```

  和 1.5 节反汇编出的游戏自身尸体标记路径完全一致。没有地图块的场景沿 `transition_points` 找最近有地图的场景。
- 每次刷新只做 `Root.SetActive(visible)`，不重建。

ScreenshotMarker 同理：`GameMap.SetupMapMarkers` postfix 里 `Instantiate(gameMap.mapMarkerTemplates[0], gameMap.markerParent)`。
CollectionPin 则克隆 `fleaPinParents[0].GetChild(0)` 并删掉 `MapPin`，坐标是手工采集的地图坐标（坐标系 未验证）。

---

## 3. 收集物种类与"已收集"判定

主要来源：Completionist Map 数据 + 存档追踪器 [Br3zzly/silksong-completionist](https://github.com/Br3zzly/silksong-completionist)（`src/dictionary/categories/*.ts`），
PlayerData 字段名已在 dump.cs 核对。**规律：地图上的拾取物几乎都是场景持久布尔；商店购买是 PlayerData bool。**

| 分类 | 世界拾取物 | 商店/NPC | 计数 |
|---|---|---|---|
| 面具碎片 | `persistentBools[scene,"Heart Piece"]` | `PurchasedBonebottomHeartPiece`(0x396) 等 | `heartPieces`(0x78) |
| 丝轴碎片 | `[scene,"Silk Spool"]` | `PurchasedBelltownSpoolSegment`(0x53C)、`purchasedGrindleSpoolPiece`(0x64C)、`MerchantEnclaveSpoolPiece`(0x6EC) | `silkSpoolParts`(0x98) |
| 工艺金属 | `[scene,"Collectable Item Pickup - Tool Metal"]` | `PurchasedBonebottomToolMetal`(0x397)、`MerchantEnclaveToolMetal`(0x6F0) | |
| 记忆挂坠 | `[scene,"Collectable Item Pickup"]`（物品 `Crest Socket Unlocker`） | `PurchasedPilgrimsRestMemoryLocket`(0x419)、`PurchasedBelltownMemoryLocket`(0x53D)、`purchasedGrindleMemoryLocket`(0x64B) | |
| 通用道具拾取 | `[scene,"Collectable Item Pickup"]` | | `PlayerData.Collectables`(0x960) |
| 丝食者 | `[scene,"Silk Grub Large Cocoon"]` | | |
| 苔莓 | `[scene,"moss_berry_fruit"]` + 若干 PD bool | | |
| 遗物 | 拾取点 persistent bool | | `PlayerData.Relics`(0x968)，`CollectableRelic.get_IsInInventory`(0x2295D20) |
| 工具 | 拾取点 persistent bool | | `PlayerData.Tools`(0x910)，`ToolItem.get_IsUnlocked`(0x26E9390) |
| 纹章 | NPC/事件 | | `PlayerData.ToolEquips`(0x8F8)，`ToolCrest.get_IsUnlocked`(0x26F3BC0) |
| 跳蚤 | — | — | 27 个 `SavedFlea_*` bool(0x810–0x82A)，另 3 个算在 `tamedGiantFlea`/`CaravanLechReturnedToCaravan`/`MetTroupeHunterWild`（后两个未在 dump 核对） |
| 钥匙 | | `HasSlabKeyA/B/C`(0x67B–D)、`CollectedDustCageKey`(0x4EE)、`collectedWardKey`(0x73F)、`MerchantEnclaveSimpleKey`、`purchasedGrindleSimpleKey`… | |
| 念珠缓存 / geo 石 | `persistentInts` / `geoRocks` | | |

世界拾取物的组件：`CollectableItemPickup`(行 36106) 同时带 `persistent : PersistentBoolItem`@0x40 和 `playerDataBool : string`@0x88，
两种追踪方式都可能出现，所以数据表每条要写明用哪种。

---

## 4. 我们的方案（exlaunch，只 hook + 调用）

### 4.1 推荐：照抄 Completionist Map 的路线（克隆玩家标记 + 自管显隐）

理由：定位用的全是游戏自己的方法（尸体标记同款），不依赖任何 prefab 里"猜"的组件；显隐判定我们自己做，不需要往托管对象里写字符串字段。

| 步骤 | 做法 | RVA |
|---|---|---|
| 刷新点 | hook `GameMap.SetupMapMarkers`（开大地图 `WorldMap`、开快速地图 `TryOpenQuickMap` 都会调）在原函数后执行 | 0x2782FC0 |
| 建 pin（每个 GameMap 实例一次） | `tpl = mapMarkerTemplates[0]`（GameMap+0xD0，数组元素从 +0x20 起）；`Object.Instantiate(tpl, markerParent(+0x200), false)` | 0x5FF5F10 |
| 去交互 | 删 `InvMarker`/`InvMarkerCollide`：`GameObject.GetComponent(string)` + `Object.Destroy`；否则标记菜单可能把它当玩家标记删除 | 0x5FE78E0 / 0x5FF65D0 |
| 定位 | `GetMapZoneFromSceneName` → `GetSceneInfo` → `GetMapPosition(worldPos, scene, sceneObj, scenePos, sceneSize)` → `Transform.set_localPosition`（z 沿用模板） | 0x277FA70 / 0x2787710 / 0x2787ED0 / 0x6001450 |
| 已绘制门槛 | `GameMapScene.get_IsMapped`（不用字符串，比查 `scenesMapped` 简单） | 0x278DAA0 |
| 全局 pin 开关 | 静态 `MapPin.get_CurrentState`，`None` 时全部隐藏 | 0x28106A0 |
| 判定：PD bool | 直接按偏移读 `PlayerData.instance`（0x2548E60）字段；偏移由生成器从 dump 解析，不写死 | — |
| 判定：场景持久布尔 | `SceneData.get_instance`(0x290CFF0) → `+0x10` → `PersistentItemDataCollection<bool,…>.TryGetValue`(0x3C14A70)，值在 `data+0x21` | 见风险 2 |
| 显隐 | `GameObject.SetActive` | 0x5FEA310 |
| 染色（可选） | `SpriteRenderer.set_color` | 0x5F40270 |
| 字符串 | `Marshal.PtrToStringAnsi(IntPtr)`；要跨帧保存就用 `GCHandle.Alloc` 固定 | 0x515E390 / 0x515C560 |

`GameMap` 是否换了实例用 `unity::isAlive`（已有）判断，换了就重建（读档、回主菜单）。

### 4.2 备选：克隆带 `MapPinConditional` 的游戏 pin，改写条件

改 `visibleCondition.PersistentBool = {Id, SceneName, false}`，由游戏自己的 `OnEnable` 判定。
缺点：要往托管对象里写字符串指针（有 GC 写屏障问题，见风险 1），并且哪个 pin 带这个组件没法从 dump 确认。只在 4.1 的持久布尔查询走不通时再考虑。

### 4.3 不采用

往 `PlayerData.placedMarkers` 塞坐标，让游戏自己生成：会写进存档，而且占用玩家标记数量。

### 4.4 要带的数据

每条记录：`scene`、`x, y`（世界坐标 = localPosition 链相加）、`kind`、判定方式
（`PB scene id` / `PD bool字段` / `PD int >= n` / 物品名），另加一张**场景尺寸表**（只放用到的场景）。
约 300 条（面具 14 + 丝轴 13 + 工艺金属 5 + 通用拾取 129 + 工具 36 + 遗物 19 + 丝食者 10 + 苔莓 4 + 跳蚤 27 + 商店若干），几十 KB。

**能不能从 PC mod 数据导出**：技术上能，结构完全对得上（Switch 和 PC 同为 v1.0.3，场景名/对象名一致——未验证逐条一致）。
但 Completionist Map **没有 license**，不能把它的 JSON 或导出结果提交进仓库。做法照 `offsets.h`：
写 `tools/` 脚本，让用户从**自己的** RomFS（UnityPy/AssetStudio）抽取，或读用户自己下载的 Completionist JSON，生成 `collectibles.gen.h`，不入库。

### 4.5 风险

1. **托管对象生命周期 / GC**：原生代码持有的字符串 GC 看不见。每次刷新现建现用最稳；要缓存就用 `GCHandle`。
   Unity 6 在 Switch 上是否开增量 GC（决定写托管字段要不要写屏障）——未验证。
2. **共享泛型要传 MethodInfo**：`TryGetValue`(0x3C14A70) 是 `<bool, object>` 共享实现，要传 `Method$SceneData.PersistentItemDataCollection<bool, SceneData.SerializableBoolData>.TryGetValue()`（metadata 槽位 0x7E34B68）。
   游戏在 `Condition.get_IsFulfilled` 里先调 `0xA69230(slot, 1)` 做惰性初始化再取值，我们照做即可。
   这是最不确定的一步，第一版可以先只做 PlayerData bool 类（跳蚤、商店物品），把风险隔开。
3. **模板结构**：`mapMarkerTemplates[0]` 的子节点和组件是 Completionist 在 PC 上看到的，Switch 上首次运行要打日志核对（子节点数、组件名）。
4. **没有地图块的场景**：`GetMapPosition` 返回 (-1000,-1000)，这类条目要么丢弃，要么离线换算到相邻场景（Completionist 用 `transition_points`）。
5. **视口/边界**：Completionist 额外 patch `GetCurrentBounds` 扩边界，否则边缘的 pin 可能滚不到——未验证是否必要。
6. **只能在主线程调用**：`SetupMapMarkers` 在主线程，满足。
7. **数据版本**：PC 数据抽取自 2025-10 的 PC 版；与 Switch v1.0.3 是否逐条一致——未验证，实机抽查几条面具碎片位置即可。

### 4.6 步骤 → 验证

| # | 步骤 | 验证 |
|---|---|---|
| 1 | hook `SetupMapMarkers`，日志打印 `mapMarkerTemplates` 长度、模板子节点/组件、`markerParent`、`fleaPinParents` 子节点名 | 日志中出现 `SavedFlea_*` 子节点名，模板结构与 PC 一致 |
| 2 | 硬编码 1 个跳蚤点：克隆模板并定位 | 开地图后标记与游戏自己的跳蚤 pin 重合 |
| 3 | PD bool 判定（跳蚤 27 个） | 救过的跳蚤不显示 |
| 4 | 场景持久布尔判定（面具碎片 14 个） | 拿过的碎片消失，没拿的显示 |
| 5 | 生成器 + 完整数据表 | 各分类数量与数据表一致 |

---

## 5. 生成器 v2：全部收集物（`tools/50-gen-collectibles.py`）

规则移植自 Completionist 源码，拆成 `tools/collectibles_tables.py`（Inaccessible、Useless、AssignStyles 等硬编码表）、
`tools/collectibles_rules.py`（persistent_bools 的剔除 / 分类 / 物品类型）、`tools/collectibles_sources.py`（其它数据源）、
`tools/collectibles_selftest.py`（`selftest` 子命令，20 项）。PlayerData 偏移只取 `dump.cs` 里 `PlayerData` 的 `bool` 字段。

### 5.1 结果（Completionist_Map-1.0.3.zip + v1.0.3 dump，共 571 条）

| 分类 | 条数 | 来源 / 判定 |
|---|---|---|
| MaskShard | 16 | 14 个拾取点 PB + 商店 PD 2（`PurchasedBonebottomHeartPiece`、`MerchantEnclaveShellFragment`） |
| SpoolFragment | 17 | 13 PB + 商店 PD 3 + `MetCaravanTroupeLeaderJudge` |
| Craftmetal | 8 | 6 PB + 商店 PD 2 |
| Tool | 34 | 31 个拾取点 PB（按 item_locations 的 `Tools` 表）+ 丝技 3（`hasNeedleThrow`/`hasThreadSphere`/`hasSilkCharge`，原版归在 Tools） |
| ToolkitUpgrade | 4 | 商店 PD |
| Materium | 0 | 见 5.2 |
| Relic | 18 | PB（`CollectableRelic` 表） |
| Memento | 2 | PB |
| MemoryLocket | 19 | 17 PB + 商店 PD 2 |
| Collectable | 51 | 钥匙 / 任务物品 / 地图（地图机与地图检视点用 `Has*Map` PD） |
| Rosary | 320 | 念珠缓存 141（PersistentIntAtLeast）、念珠石 33（GeoRockDepleted）、`Geo*`/`Craw_home*` 等 PB 111、念珠串拾取 30、可破坏物 5 |
| ShellShard | 28 | `Shell Shard*` PB + `Shard Pouch` / `Great Shard` 拾取 |
| MossBerry | 4 | `moss_berry_fruit` PB |
| SilkEater | 9 | item_locations 的 `Silk Grub`（Under_20 按原版排除） |
| Crest | 0 | 见 5.2 |
| SilkHeart | 3 | tracker_markers 位置 + Boss 击败 PD |
| Ability | 8 | playmakers 位置 + `has*` PD |
| Flea | 30 | 27 个 `SavedFlea_<场景>` + `tamedGiantFlea` / `CaravanLechReturnedToCaravan` / `MetTroupeHunterWild` |

判定方式：PersistentBool 321、PersistentIntAtLeast 141、GeoRockDepleted 33、PlayerDataBool 76。所有条目都查到了场景尺寸与 PD 偏移。

跳蚤位置来源：Completionist 自己用游戏的跳蚤 pin（`GameMap.fleaPinParents`），数据包里没有；但 `item_fsm_actions.json`
中 `SavedItemId == "FleasCollected Target"` 的 66 条正好覆盖 30 只跳蚤（每只 2~3 个变体，位置相差 < 3 单位，取第一条）。
字段名规则 `SavedFlea_<CleanScenePath>` 与游戏 pin 名一致，27 个全部在 dump 中命中。

### 5.2 跳过的及原因

| 内容 | 数量 | 原因 |
|---|---|---|
| 纹章（Toolmaster/Spell/Warrior/Reaper/Wanderer/Witch） | 6 | 原版判定 `ToolCrest.IsUnlocked`（`PlayerData.ToolEquips` 集合），没有单独 bool |
| Materium 分类 | — | 原版运行时查 `MateriumItemManager`，离线没有这张表；原版也把 Materium 显示为普通物品，这里归入 Collectable |
| NPC / FSM 给的工具与物品（Flea Brew、Magnetite Dice、Pimpilo、Mossberry 等） | 33 | 无 `LinkedPDBool`，只能靠 `ToolItem.IsUnlocked` 等运行时状态 |
| 纪念品 Memento Garmond ×5、Grey Memento ×2、Sprintmaster Memento；Materium 检视点 Magnetite/Smeltstone/Voltstone/Roach Corpse | 12 | 物体不带持久布尔也没有 linkedPDBool，原版靠 `IsListedInCollection` / `MateriumItem.IsCollected` |
| 商店里没有 `PlayerDataBoolName` 的商品（工具、Compass 等） | 22 | 同上 |
| 按 `alternateUnlockedTest` 找的工具（FindItemsByFlags 前半段） | — | 需要运行时 `ToolItemManager` |
| breakable_holders 的念珠 / 贝壳碎片容器 | 171 | `persistentInts <= 0`，没有对应 CheckKind（`PersistentIntAtLeast` 只能 >=） |
| 任务奖励、Sherma 的丝轴、Bone_12 瓢虫工具包（`pinGalleriesCompleted >= 1`） | — | 任务 / int 条件不在 4 种 CheckKind 内 |

### 5.3 与原版的差异 / 未验证

- 拾取物按 `ActualItemId` 定类（原版用 `Details`，遇到 "Deparent" 等说明文字会落到通用物品）。
- Grindle 商店里与其它店重复的商品原版只是不计数，这里去掉（第三幕才出现）。
- HiddenFromCount 的同物另一份摆放（`library_glass_heart_piece`、Slab_17 `Deparent`）在同场景已有同类条目时不重复画。
- 可见性条件没法表达：第三幕才出现 / 第三幕消失的念珠（原版 `blackThreadWorld` FlagTest）会常驻显示；钢魂专属物品（`permadeathMode` SetOn）直接去掉。
- 原版的 `Move()` 坐标修正只作用于战斗、单向墙、Bone_01b 苔莓、Void Tendrils，这几项都不在表里，所以没有移植。
- `Great Shard` 归为 ShellShard——未验证（离线推断）。
- 念珠串阈值照原版（`hitStateCount - 1`，名字含 `string` 再减 1），128 个缓存阈值为 0，即"有记录就算打过"——未验证。
- `breakable_misc` 的 5 个念珠墓碑 `Persistent == false`，按原版查 persistentBools，可能永远查不到记录——未验证。
- 子场景（`Bone_05_boss`、`Bone_East_08_boss_beastfly`、`Aqueduct_05_festival`，共 3 处）的尺寸先按 ActualScenePath 查，查不到才退到同名主场景——未验证子场景与主场景 tilemap 等大。
