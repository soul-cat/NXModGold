// 地图收集物标记的数据结构（表本身由 tools/50-gen-collectibles.py 生成到
// collectibles.gen.h，不入库）。

#pragma once

#include <cstdint>

namespace sshud::map {

enum class CollectibleKind : uint8_t {
  MaskShard,
  SpoolFragment,
  Craftmetal,
};

struct CollectibleEntry {
  const char* scene;          // 场景名，如 "Crawl_02"
  const char* persistentId;   // SceneData 持久布尔的 Id（= 拾取物 GameObject 名）
  CollectibleKind kind;
  float worldX;               // 场景内世界坐标
  float worldY;
  float sceneWidth;           // 场景 tilemap 尺寸，GameMap.GetMapPosition 需要
  float sceneHeight;
};

struct Tint {
  float r;
  float g;
  float b;
  float a;
};

/// 各分类的标记颜色（乘在模板贴图上）
constexpr Tint tintFor(CollectibleKind kind) {
  switch (kind) {
    case CollectibleKind::MaskShard:
      return {0.55f, 0.85f, 1.0f, 1.0f};  // 淡蓝
    case CollectibleKind::SpoolFragment:
      return {1.0f, 1.0f, 1.0f, 1.0f};    // 白
    case CollectibleKind::Craftmetal:
      return {1.0f, 0.62f, 0.2f, 1.0f};   // 橙
  }
  return {1.0f, 1.0f, 1.0f, 1.0f};
}

}  // namespace sshud::map
