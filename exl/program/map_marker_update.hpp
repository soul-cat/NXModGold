// 完整刷新时单个标记的处理：图标、判定显隐、按需定位、按需 SetActive。

#pragma once

#include "map_collected.hpp"
#include "map_icons.hpp"
#include "map_marker_state.hpp"
#include "map_native_fleas.hpp"
#include "map_refresh_log.hpp"

namespace sshud::map {

constexpr float NO_MAP_POSITION = -1000.0f;  // GetMapPosition 在场景缺地图块时的返回值

inline Vector2 mapPositionOf(ui::ManagedObject gameMap, const CollectibleEntry& entry, const SceneInfo& info) {
  if (!ui::isAlive(info.scene)) return {NO_MAP_POSITION, NO_MAP_POSITION};
  return call<Vector2>(GM_GETMAPPOSITION, gameMap, Vector2{entry.worldX, entry.worldY}, info.scene, info.sceneObj,
                       info.scenePos, Vector2{entry.sceneWidth, entry.sceneHeight});
}

/// 算地图坐标并挪过去；场景缺地图块时返回 false
inline bool placeMarker(ui::ManagedObject gameMap, ui::ManagedObject marker, const CollectibleEntry& entry,
                        const SceneInfo& info) {
  const Vector2 pos = mapPositionOf(gameMap, entry, info);
  if (pos.x <= NO_MAP_POSITION) return false;
  ui::ManagedObject transform = transformOf(marker);
  const ui::Vector3 current = call<ui::Vector3>(TR_GETLOCALPOSITION, transform);
  call<void>(TR_SETLOCALPOSITION, transform, ui::Vector3{pos.x, pos.y, current.z});
  return true;
}

#if SSHUD_HAS_COLLECTIBLES
struct RefreshContext {
  MarkerState& state;
  ui::ManagedObject gameMap;
  const IconSet* icons;  // null = 本次不处理图标
  const NativeFleaPins& nativeFleas;
};

enum class MarkerFate : uint8_t { Visible, Unplaceable, Unmapped, NoAreaMap, Collected, NativePin };

/// 不用算位置就能判定的隐藏原因；都不是时返回 Visible
inline MarkerFate fateBeforePlacing(const CollectibleEntry& entry, const EntryStrings& strings,
                                    const SceneInfo& info) {
  if (!ui::isAlive(info.scene)) return MarkerFate::Unplaceable;
  if (!info.isMapped) return MarkerFate::Unmapped;
  if (!info.isDrawn) return MarkerFate::NoAreaMap;
  return isCollected(entry, strings) ? MarkerFate::Collected : MarkerFate::Visible;
}

/// 只给要显示且还没定位的标记定位；场景变回未绘制时作废，重新绘制后再定位一次
inline MarkerFate placeIfNeeded(const RefreshContext& ctx, size_t i, const SceneInfo& info, MarkerFate fate) {
  MarkerBlock<COLLECTIBLE_COUNT>& block = ctx.state.block;
  if (fate == MarkerFate::Unmapped) block.setPlaced(i, false);
  if (fate != MarkerFate::Visible || block.isPlaced(i)) return fate;
  if (!placeMarker(ctx.gameMap, block.at(i), COLLECTIBLES[i], info)) return MarkerFate::Unplaceable;
  block.setPlaced(i, true);
  return fate;
}

inline void countFate(RefreshStats& stats, MarkerFate fate, CollectibleKind kind) {
  switch (fate) {
    case MarkerFate::Visible:
      ++stats.visible;
      ++stats.visibleByKind[static_cast<size_t>(kind)];
      return;
    case MarkerFate::Unplaceable:
      ++stats.unplaceable;
      return;
    case MarkerFate::Unmapped:
      ++stats.unmapped;
      return;
    case MarkerFate::NoAreaMap:
      ++stats.noAreaMap;
      return;
    case MarkerFate::Collected:
      ++stats.collected;
      return;
    case MarkerFate::NativePin:
      ++stats.nativePin;
      return;
  }
}

/// 要显示的跳蚤标记若与游戏原生跳蚤图钉重复（map_native_fleas.hpp），改为隐藏
inline MarkerFate dropNativeDuplicate(const RefreshContext& ctx, size_t i, MarkerFate fate) {
  const CollectibleEntry& entry = COLLECTIBLES[i];
  if (fate != MarkerFate::Visible || entry.kind != CollectibleKind::Flea || ctx.nativeFleas.count == 0) return fate;
  const ui::Vector3 position = call<ui::Vector3>(TR_GETPOSITION, transformOf(ctx.state.block.at(i)));
  const float distance = ctx.nativeFleas.nearest(entry.scene, position);
  if (distance < NATIVE_FLEA_MATCH_DISTANCE) return MarkerFate::NativePin;
  logUnmatchedFleaOnce(i, entry.scene, distance);
  return fate;
}

inline void updateMarker(const RefreshContext& ctx, size_t i, RefreshStats& stats) {
  ui::ManagedObject marker = ctx.state.block.at(i);
  if (marker == nullptr) return;
  const CollectibleEntry& entry = COLLECTIBLES[i];
  bool& hasIcon = ctx.state.icons.hasIcon[i];
  if (ctx.icons != nullptr && !hasIcon) hasIcon = applyIcon(marker, ctx.icons->resolve(entry));
  stats.iconless += hasIcon ? 0 : 1;
  const EntryStrings& strings = ctx.state.strings.get(i, entry);
  const SceneInfo& info = ctx.state.scenes.get(i, ctx.gameMap, entry.scene, strings.scene);
  const MarkerFate placed = placeIfNeeded(ctx, i, info, fateBeforePlacing(entry, strings, info));
  const MarkerFate fate = dropNativeDuplicate(ctx, i, placed);
  ctx.state.block.setMarkerActive(i, fate == MarkerFate::Visible);
  countFate(stats, fate, entry.kind);
}
#endif

}  // namespace sshud::map
