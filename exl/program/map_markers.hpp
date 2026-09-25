// 地图收集物标记：没拿到就显示，拿到就隐藏（效果对齐游戏的跳蚤标记）。
//
// 做法照 Completionist Map（docs/tasks/research-collectible-map.md 4.1）：
//   - 刷新点：GameMap.SetupMapMarkers 与 GameMap.EnableUnlockedAreas 之后。大地图（WorldMap）先
//     SetupMapMarkers 再 EnableUnlockedAreas，快速地图反过来；区域激活状态只有在后者之后才准
//     （实机日志：只挂前者时大地图 570 个全被判成"无区域地图"）。每次开图只在后者之后完整刷新一次，
//     前者之后通常什么也不做，决策与理由见 map_refresh_plan.hpp。
//   - 建标记：克隆玩家手动标记模板 mapMarkerTemplates[0] 到 markerParent 下，
//     删掉 InvMarker / InvMarkerCollide（否则标记菜单会把它当玩家标记处理）和第 0 个子节点，
//     换成游戏自带图标（map_icons.hpp）；图标没找到时退回按分类染色的圆点。
//   - 定位：GetMapZoneFromSceneName → GetSceneInfo → GetMapPosition（游戏放尸体标记同款）。
//     只给要显示的标记定位，每个标记在一次缓存生命期内只定位一次（场景变回未绘制后作废）。
//   - 显隐：地图标记开关（toggles.hpp）&& 全局 pin 开关 != None && 场景已绘制 && 未收集（map_collected.hpp）
//     && 跳蚤没有被游戏原生跳蚤图钉标着（map_native_fleas.hpp）。
//     前两项任一关闭时只把已建的标记全隐藏，不做任何查询。SetActive 只在和上次设的不同时才调。
//   - 刷新前先调 GameManager.SaveLevelState：游戏平时只在离开房间时把房间内物品状态写进 SceneData，
//     不调的话在同一房间捡完东西马上开图，图标还在。
//
// 跨帧持有的托管指针（标记、场景、字符串、贴图）一律用 GCHandle 钉住（原生静态区 GC 扫不到），
// 标记组见 map_marker_block.hpp。日志只在结果变化时写，耗时每 TIMING_LOG_INTERVAL 次完整刷新汇总一行。

#pragma once

#include "map_calls.hpp"
#include "map_marker_update.hpp"
#include "map_refresh_plan.hpp"
#include "toggles.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

namespace sshud::map {

constexpr int32_t PIN_STATE_NONE = 2;          // MapPin.PinVisibilityStates.None
constexpr size_t MARKER_NAME_CAPACITY = 32;
constexpr int MAX_LOGGED_MISSING_ICONS = 8;

inline int32_t pinVisibilityState() { return call<int32_t>(MAPPIN_GETSTATE); }

/// 我们的开关开着且全局 pin 状态不是 None
inline bool isMarkersEnabled() {
  return toggles::g_settings.isEnabled(toggles::Feature::MapMarkers) && pinVisibilityState() != PIN_STATE_NONE;
}

// ---------------------------------------------------------------- 标记的建立
inline ui::ManagedObject markerTemplate(ui::ManagedObject gameMap) {
  const void* templates = ui::readField<const void*>(gameMap, GM_MARKERTEMPLATES);
  return templates == nullptr || arrayLength(templates) == 0 ? nullptr : arrayItem(templates, 0);
}

/// 新建一个标记（放在 parent 最后）；返回其 GameObject，失败返回 null
inline ui::ManagedObject createMarker(ui::ManagedObject tpl, ui::ManagedObject parent, size_t index,
                                      CollectibleKind kind) {
  ui::ManagedObject marker = call<ui::ManagedObject>(UOBJ_INSTANTIATE, tpl, parent, false);
  if (marker == nullptr) return nullptr;
  char name[MARKER_NAME_CAPACITY];
  std::snprintf(name, sizeof(name), "sshud_pin_%zu", index);
  call<void>(UOBJ_SETNAME, marker, managedString(name));
  for (const char* interactive : {"InvMarker", "InvMarkerCollide"}) {
    ui::ManagedObject component = componentByName(marker, interactive);
    if (ui::isAlive(component)) destroyObject(component);
  }
  ui::ManagedObject transform = transformOf(marker);
  if (childCount(transform) > 0) destroyObject(gameObjectOf(childAt(transform, 0)));
  ui::ManagedObject renderer = componentByName(marker, "SpriteRenderer");
  const Tint tint = tintFor(kind);
  if (ui::isAlive(renderer)) call<void>(SR_SETCOLOR, renderer, ui::Color{tint.r, tint.g, tint.b, tint.a});
  return marker;
}

#if SSHUD_HAS_COLLECTIBLES
/// 丢掉旧标记组、建一组新的；场景缓存与图标进度跟着作废
inline void rebuildMarkers(MarkerState& state, const MapParts& parts) {
  state.block.beginBlock(parts.parent);
  state.scenes.reset();
  state.icons = IconProgress{};
  for (size_t i = 0; i < COLLECTIBLE_COUNT; ++i) {
    ui::ManagedObject marker = createMarker(parts.tpl, parts.parent, i, COLLECTIBLES[i].kind);
    if (marker == nullptr) continue;
    state.block.add(i, marker);
    ++state.unreportedCreated;
  }
}

inline void ensureMarkers(MarkerState& state, const MapParts& parts) {
  if (!state.block.isIntact(parts.parent)) rebuildMarkers(state, parts);
}

// ---------------------------------------------------------------- 图标
inline void registerSpriteNames(SpriteLookup& sprites) {
  for (const CollectibleEntry& entry : COLLECTIBLES) {
    if (entry.iconSource == IconSource::SpriteName) sprites.want(entry.iconName);
  }
}

/// 准备本次开图的图标来源：按名字的贴图从钉住的缓存取，还缺且扫描次数没用完才扫一遍
inline void loadPendingIcons(IconSet& icons, const MapParts& parts, IconProgress& progress) {
  SpriteLookup& sprites = spriteLookup();
  if (!sprites.hasNames()) registerSpriteNames(sprites);
  sprites.evictDead();
  if (sprites.canScan()) sprites.scan(spriteOf(parts.tpl));
  icons.load(parts.gameMap);
  ++progress.passes;
}

/// 处理次数用完后仍没图标的贴图名只记一次（去重，最多 MAX_LOGGED_MISSING_ICONS 个）
inline void logMissingIconsOnce(const IconProgress& progress) {
  static bool s_hasLogged = false;
  if (s_hasLogged || progress.isPending()) return;
  s_hasLogged = true;
  const char* logged[MAX_LOGGED_MISSING_ICONS] = {};
  int count = 0;
  for (size_t i = 0; i < COLLECTIBLE_COUNT && count < MAX_LOGGED_MISSING_ICONS; ++i) {
    const CollectibleEntry& entry = COLLECTIBLES[i];
    if (progress.hasIcon[i] || std::find(logged, logged + count, entry.iconName) != logged + count) continue;
    logged[count++] = entry.iconName;
    Logging.Log("[sshud] map: no icon for source=%d name='%s' (e.g. %s/%s)", static_cast<int>(entry.iconSource),
                entry.iconName, entry.scene, entry.persistentId);
  }
}

// ---------------------------------------------------------------- 刷新
/// 让当前房间的持久物品把状态写进 SceneData（与离开房间时相同）
inline void saveCurrentRoomState() {
  ui::ManagedObject gameManager = call<ui::ManagedObject>(GAMEMGR_GETINSTANCE);
  if (ui::isAlive(gameManager)) call<void>(GAMEMGR_SAVELEVELSTATE, gameManager);
}

/// 标记关闭：只把已建的标记全隐藏，不做任何查询；旧组已被（部分）销毁时连残存的一起丢掉，
/// 场景缓存也放开（下次开启时重建）
inline void hideMarkers(MarkerState& state, ui::ManagedObject parent) {
  if (state.block.isIntact(parent)) return state.block.hideAll();
  state.block.discard();
  state.scenes.reset();
}

inline void fullRefresh(MarkerState& state, const MapParts& parts, RefreshHook hook) {
  const uint64_t startTick = systemTick();
  saveCurrentRoomState();
  ensureMarkers(state, parts);
  const bool wantsIcons = hook == RefreshHook::EnableUnlockedAreas && state.icons.isPending();
  IconSet icons;  // 含托管指针，必须在栈上（GC 会扫栈）
  if (wantsIcons) loadPendingIcons(icons, parts, state.icons);
  RefreshStats stats;
  stats.created = std::exchange(state.unreportedCreated, 0);
  state.scenes.beginRefresh();
  const NativeFleaPins nativeFleas = collectNativeFleaPins(parts.gameMap);
  const RefreshContext ctx{state, parts.gameMap, wantsIcons ? &icons : nullptr, nativeFleas};
  for (size_t i = 0; i < COLLECTIBLE_COUNT; ++i) updateMarker(ctx, i, stats);
  logRefresh(stats, elapsedMicros(startTick), COLLECTIBLE_COUNT);
  if (wantsIcons && stats.iconless > 0) logMissingIconsOnce(state.icons);
}

inline void runAction(MarkerState& state, const MapParts& parts, RefreshAction action, RefreshHook hook) {
  switch (action) {
    case RefreshAction::Skip:
      return;
    case RefreshAction::Hide:
      return hideMarkers(state, parts.parent);
    case RefreshAction::Create:
      return rebuildMarkers(state, parts);
    case RefreshAction::Full:
      return fullRefresh(state, parts, hook);
  }
}

inline void refreshMarkers(ui::ManagedObject gameMap, RefreshHook hook) {
  logMapAllRoomsChange();
  const MapParts parts{gameMap, ui::readField<ui::ManagedObject>(gameMap, GM_MARKERPARENT), markerTemplate(gameMap)};
  if (!ui::isAlive(parts.parent) || !ui::isAlive(parts.tpl)) {
    Logging.Log("[sshud] map: no marker template/parent (tpl=%p parent=%p)", parts.tpl, parts.parent);
    return;
  }
  MarkerState& state = markerState();
  const RefreshInputs inputs{hook, isMarkersEnabled(), state.block.isIntact(parts.parent), state.block.mode()};
  const RefreshAction action = decideRefresh(inputs);
  runAction(state, parts, action, hook);
  state.block.setMode(modeAfter(action, state.block.mode()));
}
#endif

}  // namespace sshud::map

#if SSHUD_HAS_COLLECTIBLES
HOOK_DEFINE_TRAMPOLINE(SetupMapMarkersHook) {
  static void Callback(void* self, const void* method) {
    Orig(self, method);
    if (sshud::unity::isAlive(self)) sshud::map::refreshMarkers(self, sshud::map::RefreshHook::SetupMapMarkers);
  }
};

// IL2CPP 签名：void (GameMap* self, Nullable<MapZone> setCurrent, const MethodInfo* method)
//   Nullable<MapZone> = { bool hasValue; int32 value; } 共 8 字节，按值放在 x1，原样转发。
HOOK_DEFINE_TRAMPOLINE(EnableUnlockedAreasHook) {
  static void Callback(void* self, uint64_t setCurrent, const void* method) {
    Orig(self, setCurrent, method);
    if (sshud::unity::isAlive(self)) sshud::map::refreshMarkers(self, sshud::map::RefreshHook::EnableUnlockedAreas);
  }
};
#endif

namespace sshud::map {

inline void installHooks() {
#if SSHUD_HAS_COLLECTIBLES
  SetupMapMarkersHook::InstallAtOffset(GM_SETUPMAPMARKERS);
  EnableUnlockedAreasHook::InstallAtOffset(GM_ENABLEUNLOCKEDAREAS);
  Logging.Log("[sshud] map hooks installed: %zu collectibles", COLLECTIBLE_COUNT);
#else
  Logging.Log("[sshud] map: collectibles.gen.h missing, map markers disabled");
#endif
}

}  // namespace sshud::map
