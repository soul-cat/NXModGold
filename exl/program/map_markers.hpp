// 地图收集物标记：没拿到就显示，拿到就隐藏（效果对齐游戏的跳蚤标记）。
//
// 做法照 Completionist Map（docs/tasks/research-collectible-map.md 4.1）：
//   - 刷新点：GameMap.SetupMapMarkers 与 GameMap.EnableUnlockedAreas 之后。大地图（WorldMap）先
//     SetupMapMarkers 再 EnableUnlockedAreas，快速地图反过来；区域激活状态只有在后者之后才准，
//     所以两处都刷新（实机日志：只挂前者时大地图 570 个全被判成"无区域地图"）。
//   - 建标记：克隆玩家手动标记模板 mapMarkerTemplates[0] 到 markerParent 下，
//     删掉 InvMarker / InvMarkerCollide（否则标记菜单会把它当玩家标记处理）和第 0 个子节点，
//     换成游戏自带图标（map_icons.hpp）；图标没找到时退回按分类染色的圆点。
//   - 定位：GetMapZoneFromSceneName → GetSceneInfo → GetMapPosition（游戏放尸体标记同款）。
//   - 显隐：地图标记开关（toggles.hpp）&& 全局 pin 开关 != None && 场景已绘制 && 未收集（map_collected.hpp）。
//     前两项任一关闭时只把已建的标记全隐藏，不做任何查询。
//   - 刷新前先调 GameManager.SaveLevelState：游戏平时只在离开房间时把房间内物品状态写进 SceneData，
//     不调的话在同一房间捡完东西马上开图，图标还在。
//
// 托管对象全部"现取现用"，不跨帧持有托管指针（原生持有的引用 GC 看不见）：
// 我们的标记在 markerParent 下是连续一段子节点，每次刷新 Find("sshud_pin_0")
// 取起点，其余按下标 GetChild —— O(n)。
// 开图卡顿相关：查询用的托管字符串钉住复用（map_strings.hpp）；日志只在结果变化时写，
// 耗时每 TIMING_LOG_INTERVAL 次刷新汇总一行（map_timing.hpp）。

#pragma once

#include "map_calls.hpp"
#include "map_collected.hpp"
#include "map_icons.hpp"
#include "map_markers_data.hpp"
#include "map_strings.hpp"
#include "map_refresh_log.hpp"
#include "map_scene_cache.hpp"
#include "toggles.hpp"

#if __has_include("collectibles.gen.h")
#include "collectibles.gen.h"
#define SSHUD_HAS_COLLECTIBLES 1
#else
#define SSHUD_HAS_COLLECTIBLES 0
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace sshud::map {

constexpr int32_t PIN_STATE_NONE = 2;          // MapPin.PinVisibilityStates.None
constexpr float NO_MAP_POSITION = -1000.0f;    // GetMapPosition 在场景缺地图块时的返回值
constexpr size_t MARKER_NAME_CAPACITY = 32;
constexpr const char* FIRST_MARKER_NAME = "sshud_pin_0";
constexpr int MAX_ICON_ATTEMPTS = 5;         // 贴图可能要等相关界面载入过才找得到，开几次图后不再重试
constexpr int MAX_LOGGED_MISSING_ICONS = 8;

inline int32_t pinVisibilityState() { return call<int32_t>(MAPPIN_GETSTATE); }


inline Vector2 mapPositionOf(ui::ManagedObject gameMap, const CollectibleEntry& entry, const SceneInfo& info) {
  if (!ui::isAlive(info.scene)) return {NO_MAP_POSITION, NO_MAP_POSITION};
  return call<Vector2>(GM_GETMAPPOSITION, gameMap, Vector2{entry.worldX, entry.worldY}, info.scene, info.sceneObj,
                       info.scenePos, Vector2{entry.sceneWidth, entry.sceneHeight});
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

// ---------------------------------------------------------------- 刷新
/// 算地图坐标并挪过去；场景缺地图块时返回 false
inline bool placeMarker(ui::ManagedObject gameMap, ui::ManagedObject marker, const CollectibleEntry& entry,
                        const SceneInfo& info) {
  ScopedTicks timer(g_profile.position);
  const Vector2 pos = mapPositionOf(gameMap, entry, info);
  if (pos.x <= NO_MAP_POSITION) return false;
  ui::ManagedObject transform = transformOf(marker);
  const ui::Vector3 current = call<ui::Vector3>(TR_GETLOCALPOSITION, transform);
  call<void>(TR_SETLOCALPOSITION, transform, ui::Vector3{pos.x, pos.y, current.z});
  return true;
}

inline bool isCollectedTimed(const CollectibleEntry& entry, const EntryStrings& strings) {
  ScopedTicks timer(g_profile.collected);
  return isCollected(entry, strings);
}


inline void updateMarker(ui::ManagedObject gameMap, ui::ManagedObject marker, const CollectibleEntry& entry,
                         const EntryStrings& strings, const SceneInfo& info, RefreshStats& stats) {
  bool isVisible = false;
  if (!placeMarker(gameMap, marker, entry, info)) {
    ++stats.unplaceable;
  } else if (!info.isMapped) {
    ++stats.unmapped;
  } else if (!info.isDrawn) {
    ++stats.noAreaMap;
  } else if (isCollectedTimed(entry, strings)) {
    ++stats.collected;
  } else {
    isVisible = true;
  }
  {
    ScopedTicks timer(g_profile.activate);
    setActive(marker, isVisible);
  }
  stats.visible += isVisible ? 1 : 0;
  stats.visibleByKind[static_cast<size_t>(entry.kind)] += isVisible ? 1 : 0;
}

#if SSHUD_HAS_COLLECTIBLES
constexpr size_t COLLECTIBLE_COUNT = sizeof(COLLECTIBLES) / sizeof(COLLECTIBLES[0]);

template <size_t N>
const SceneInfo& sceneInfoTimed(SceneCache<N>& scenes, size_t index, ui::ManagedObject gameMap,
                                const EntryStrings& strings) {
  ScopedTicks timer(g_profile.sceneQuery);
  return scenes.get(index, gameMap, COLLECTIBLES[index].scene, strings.scene);
}

/// 我们的标记在 parent 下的起始下标；还没建过（或数量对不上）返回 -1
inline int32_t firstMarkerIndex(ui::ManagedObject parent) {
  ui::ManagedObject first = findChild(parent, FIRST_MARKER_NAME);
  if (!ui::isAlive(first)) return -1;
  const int32_t start = siblingIndex(first);
  return childCount(parent) - start >= static_cast<int32_t>(COLLECTIBLE_COUNT) ? start : -1;
}

/// 图标进度：原生 bool 表，不含托管指针，可以放静态区
struct IconProgress {
  bool hasIcon[COLLECTIBLE_COUNT] = {};
  int attempts = 0;

  bool isPending() const {
    if (attempts >= MAX_ICON_ATTEMPTS) return false;
    for (bool done : hasIcon) {
      if (!done) return true;
    }
    return false;
  }
};

inline IconProgress& iconProgress() {
  static IconProgress s_progress;
  return s_progress;
}

inline void loadPendingIcons(IconSet& icons, ui::ManagedObject gameMap, ui::ManagedObject tpl) {
  IconProgress& progress = iconProgress();
  for (size_t i = 0; i < COLLECTIBLE_COUNT; ++i) {
    if (!progress.hasIcon[i] && COLLECTIBLES[i].iconSource == IconSource::SpriteName) {
      icons.byName.want(COLLECTIBLES[i].iconName);
    }
  }
  icons.load(gameMap, tpl);
  ++progress.attempts;
}

/// 列出仍没图标的贴图名（去重，最多 MAX_LOGGED_MISSING_ICONS 个），方便下一轮对名字
inline void logMissingIcons() {
  const IconProgress& progress = iconProgress();
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

/// 让当前房间的持久物品把状态写进 SceneData（与离开房间时相同）
inline void saveCurrentRoomState() {
  ui::ManagedObject gameManager = call<ui::ManagedObject>(GAMEMGR_GETINSTANCE);
  if (ui::isAlive(gameManager)) call<void>(GAMEMGR_SAVELEVELSTATE, gameManager);
}

/// 标记关闭（我们的开关或游戏的全局 pin 开关）：只把已建的标记全隐藏，不做任何查询
inline void hideMarkers(ui::ManagedObject parent) {
  const int32_t start = firstMarkerIndex(parent);
  if (start < 0) return;
  for (size_t i = 0; i < COLLECTIBLE_COUNT; ++i) {
    setActive(gameObjectOf(childAt(parent, start + static_cast<int32_t>(i))), false);
  }
}

inline void refreshMarkers(ui::ManagedObject gameMap) {
  const uint64_t startTick = systemTick();
  logMapAllRoomsChange();
  ui::ManagedObject parent = ui::readField<ui::ManagedObject>(gameMap, GM_MARKERPARENT);
  ui::ManagedObject tpl = markerTemplate(gameMap);
  if (!ui::isAlive(parent) || !ui::isAlive(tpl)) {
    Logging.Log("[sshud] map: no marker template/parent (tpl=%p parent=%p)", tpl, parent);
    return;
  }
  if (!toggles::g_settings.isEnabled(toggles::Feature::MapMarkers) || pinVisibilityState() == PIN_STATE_NONE) {
    return hideMarkers(parent);
  }
  g_profile = RefreshProfile{};
  {
    ScopedTicks timer(g_profile.roomSave);
    saveCurrentRoomState();
  }
  const int32_t start = firstMarkerIndex(parent);
  if (start < 0) iconProgress() = IconProgress{};
  const bool wantsIcons = iconProgress().isPending();
  IconSet icons;  // 含托管指针，必须在栈上（GC 会扫栈）
  if (wantsIcons) loadPendingIcons(icons, gameMap, tpl);
  RefreshStats stats;
  static SceneCache<COLLECTIBLE_COUNT> s_scenes;  // 跨刷新保留，见 map_scene_cache.hpp
  static PinnedEntryStrings<COLLECTIBLE_COUNT> s_strings;
  s_scenes.beginRefresh(gameMap, start < 0);
  for (size_t i = 0; i < COLLECTIBLE_COUNT; ++i) {
    ui::ManagedObject marker = nullptr;
    {
      ScopedTicks timer(g_profile.lookup);
      marker = start >= 0 ? gameObjectOf(childAt(parent, start + static_cast<int32_t>(i)))
                          : createMarker(tpl, parent, i, COLLECTIBLES[i].kind);
    }
    if (marker == nullptr) continue;
    stats.created += start >= 0 ? 0 : 1;
    bool& hasIcon = iconProgress().hasIcon[i];
    if (wantsIcons && !hasIcon) hasIcon = applyIcon(marker, icons.resolve(COLLECTIBLES[i]));
    stats.iconless += hasIcon ? 0 : 1;
    const EntryStrings& strings = s_strings.get(i, COLLECTIBLES[i]);
    const SceneInfo& info = sceneInfoTimed(s_scenes, i, gameMap, strings);
    updateMarker(gameMap, marker, COLLECTIBLES[i], strings, info, stats);
  }
  logRefresh(stats, elapsedMicros(startTick), COLLECTIBLE_COUNT);
  logProfile();
  if (wantsIcons && stats.iconless > 0) logMissingIcons();
}
#endif

}  // namespace sshud::map

#if SSHUD_HAS_COLLECTIBLES
HOOK_DEFINE_TRAMPOLINE(SetupMapMarkersHook) {
  static void Callback(void* self, const void* method) {
    Orig(self, method);
    if (sshud::unity::isAlive(self)) sshud::map::refreshMarkers(self);
  }
};

// IL2CPP 签名：void (GameMap* self, Nullable<MapZone> setCurrent, const MethodInfo* method)
//   Nullable<MapZone> = { bool hasValue; int32 value; } 共 8 字节，按值放在 x1，原样转发。
HOOK_DEFINE_TRAMPOLINE(EnableUnlockedAreasHook) {
  static void Callback(void* self, uint64_t setCurrent, const void* method) {
    Orig(self, setCurrent, method);
    if (sshud::unity::isAlive(self)) sshud::map::refreshMarkers(self);
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
