// 地图收集物标记跨刷新保留的状态（标记组、场景缓存、钉住的字符串、图标进度）。
// 其中的托管指针都已钉住，可以放静态区。

#pragma once

#include "map_marker_block.hpp"
#include "map_markers_data.hpp"
#include "map_scene_cache.hpp"
#include "map_strings.hpp"

#if __has_include("collectibles.gen.h")
#include "collectibles.gen.h"
#define SSHUD_HAS_COLLECTIBLES 1
#else
#define SSHUD_HAS_COLLECTIBLES 0
#endif

namespace sshud::map {

constexpr int MAX_ICON_ATTEMPTS = 5;  // 每组标记最多在几次开图里处理图标（贴图扫描另有 MAX_ICON_SCANS）

#if SSHUD_HAS_COLLECTIBLES
constexpr size_t COLLECTIBLE_COUNT = sizeof(COLLECTIBLES) / sizeof(COLLECTIBLES[0]);

/// 这组标记的图标进度：原生 bool 表，随标记重建清零
struct IconProgress {
  bool hasIcon[COLLECTIBLE_COUNT] = {};
  int passes = 0;  // 处理过几次图标（只在 EnableUnlockedAreas 之后处理，即每次开图最多一次）

  bool isPending() const {
    if (passes >= MAX_ICON_ATTEMPTS) return false;
    for (bool done : hasIcon) {
      if (!done) return true;
    }
    return false;
  }
};

/// 跨刷新保留的全部状态；托管指针都已钉住，可以放静态区
struct MarkerState {
  MarkerBlock<COLLECTIBLE_COUNT> block;
  SceneCache<COLLECTIBLE_COUNT> scenes;
  PinnedEntryStrings<COLLECTIBLE_COUNT> strings;
  IconProgress icons;
  int unreportedCreated = 0;  // 新建了但还没进统计日志的标记数
};

inline MarkerState& markerState() {
  static MarkerState s_state;
  return s_state;
}

struct MapParts {
  ui::ManagedObject gameMap;
  ui::ManagedObject parent;  // markerParent
  ui::ManagedObject tpl;     // mapMarkerTemplates[0]
};
#endif

}  // namespace sshud::map
