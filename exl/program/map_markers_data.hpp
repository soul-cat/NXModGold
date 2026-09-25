// 地图收集物标记的数据结构（表本身由 tools/50-gen-collectibles.py 生成到
// collectibles.gen.h，不入库）。本文件是生成器与运行时之间的契约，两边必须同步改。

#pragma once

#include <cstddef>
#include <cstdint>

namespace sshud::map {

/// 显示分类（决定标记颜色）。与生成器 tools/collectibles_rules.py 的 KIND_NAMES 同序。
enum class CollectibleKind : uint8_t {
  MaskShard,       // 面具碎片
  SpoolFragment,   // 丝轴碎片
  Craftmetal,      // 工艺金属
  Tool,            // 工具
  ToolkitUpgrade,  // 工具包升级
  Materium,        // 典藏
  Relic,           // 遗物
  Memento,         // 纪念品
  MemoryLocket,    // 记忆挂坠
  Collectable,     // 其它可收集道具（钥匙、任务物品等）
  Rosary,          // 念珠（缓存 / 念珠石 / 念珠串）
  ShellShard,      // 贝壳碎片
  MossBerry,       // 苔莓
  SilkEater,       // 丝食者
  Crest,           // 纹章
  SilkHeart,       // 丝之心
  Ability,         // 能力
  Flea,            // 跳蚤
};

constexpr size_t COLLECTIBLE_KIND_COUNT = static_cast<size_t>(CollectibleKind::Flea) + 1;

/// 日志用的分类名（与上面同序）
constexpr const char* COLLECTIBLE_KIND_NAMES[COLLECTIBLE_KIND_COUNT] = {
    "MaskShard", "SpoolFragment", "Craftmetal", "Tool",    "ToolkitUpgrade", "Materium",
    "Relic",     "Memento",       "MemoryLocket", "Collectable", "Rosary", "ShellShard",
    "MossBerry", "SilkEater",     "Crest",      "SilkHeart", "Ability",   "Flea",
};

/// "已收集"的判定方式
enum class CheckKind : uint8_t {
  PersistentBool,        // SceneData.persistentBools[scene, id] == true
  PersistentIntAtLeast,  // SceneData.persistentInts[scene, id] >= threshold（念珠缓存）
  GeoRockDepleted,       // SceneData.geoRocks[scene, id] 存在且 <= 0（念珠石打空）
  PlayerDataBool,        // PlayerData 在 playerDataOffset 处的 bool == true
};

/// 主判定没过时再查的物品状态（Completionist 判定第 3 步）：工具 / 遗物拾取后多数不留场景持久布尔
enum class ItemCheck : uint8_t {
  None,
  ToolUnlocked,    // ToolItemManager.GetToolByName(itemName).IsUnlocked
  RelicCollected,  // CollectableRelicManager.GetRelicData(GetRelic(itemName)).IsCollected
};

/// 图标从哪来（照 Completionist Map 的 MapStyles：全用游戏自带贴图）
enum class IconSource : uint8_t {
  SpriteName,       // Resources.FindObjectsOfTypeAll(Sprite) 里按贴图名找（iconName）
  SmallGeo,         // Gameplay.SmallGeoPrefab 的贴图（念珠）
  FleaPin,          // 游戏自己跳蚤标记的贴图（缩放再乘它的 localScale）
  CollectableItem,  // CollectableItemManager.GetItemByName(iconName).GetCollectionIcon()
};

struct CollectibleEntry {
  const char* scene;          // 场景名，如 "Crawl_02"（定位用；PlayerDataBool 也要）
  const char* persistentId;   // SceneData 条目 Id（PlayerDataBool 时为字段名，仅日志用）
  CollectibleKind kind;
  CheckKind check;
  int32_t threshold;          // PersistentIntAtLeast 用
  uint32_t playerDataOffset;  // PlayerDataBool 用：PlayerData 字段偏移（生成器从 dump.cs 解析）
  float worldX;               // 场景内世界坐标
  float worldY;
  float sceneWidth;           // 场景 tilemap 尺寸，GameMap.GetMapPosition 需要
  float sceneHeight;
  IconSource iconSource;
  const char* iconName;       // SpriteName / CollectableItem 用
  float iconScale;            // 标记的 localScale
  ItemCheck itemCheck;
  const char* itemName;       // ItemCheck 用：ToolItem / CollectableRelic 的名字（None 时为空串）
};

struct Tint {
  float r;
  float g;
  float b;
  float a;
};

/// 找不到图标时的退路：模板圆点按分类染色
constexpr Tint tintFor(CollectibleKind kind) {
  switch (kind) {
    case CollectibleKind::MaskShard:      return {0.55f, 0.85f, 1.00f, 1.0f};  // 淡蓝
    case CollectibleKind::SpoolFragment:  return {1.00f, 1.00f, 1.00f, 1.0f};  // 白
    case CollectibleKind::Craftmetal:     return {1.00f, 0.62f, 0.20f, 1.0f};  // 橙
    case CollectibleKind::Tool:           return {0.95f, 0.30f, 0.30f, 1.0f};  // 红
    case CollectibleKind::ToolkitUpgrade: return {0.85f, 0.45f, 0.20f, 1.0f};  // 赭
    case CollectibleKind::Materium:       return {0.60f, 0.90f, 0.70f, 1.0f};  // 薄荷
    case CollectibleKind::Relic:          return {0.80f, 0.60f, 1.00f, 1.0f};  // 淡紫
    case CollectibleKind::Memento:        return {1.00f, 0.75f, 0.85f, 1.0f};  // 粉
    case CollectibleKind::MemoryLocket:   return {1.00f, 0.90f, 0.40f, 1.0f};  // 金
    case CollectibleKind::Collectable:    return {0.75f, 0.75f, 0.75f, 1.0f};  // 灰
    case CollectibleKind::Rosary:         return {0.95f, 0.85f, 0.60f, 1.0f};  // 米黄
    case CollectibleKind::ShellShard:     return {0.40f, 0.80f, 0.85f, 1.0f};  // 青
    case CollectibleKind::MossBerry:      return {0.45f, 0.85f, 0.35f, 1.0f};  // 绿
    case CollectibleKind::SilkEater:      return {0.90f, 0.90f, 0.70f, 1.0f};  // 奶白
    case CollectibleKind::Crest:          return {1.00f, 0.80f, 0.20f, 1.0f};  // 亮金
    case CollectibleKind::SilkHeart:      return {1.00f, 0.40f, 0.60f, 1.0f};  // 玫红
    case CollectibleKind::Ability:        return {0.40f, 0.60f, 1.00f, 1.0f};  // 蓝
    case CollectibleKind::Flea:           return {0.70f, 1.00f, 0.40f, 1.0f};  // 黄绿
  }
  return {1.0f, 1.0f, 1.0f, 1.0f};
}

}  // namespace sshud::map
