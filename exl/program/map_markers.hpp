// 地图收集物标记：没拿到就显示，拿到就隐藏（效果对齐游戏的跳蚤标记）。
//
// 做法照 Completionist Map（docs/tasks/research-collectible-map.md 4.1）：
//   - 刷新点：GameMap.SetupMapMarkers 之后（开大地图 / 快速地图都会调）。
//   - 建标记：克隆玩家手动标记模板 mapMarkerTemplates[0] 到 markerParent 下，
//     删掉 InvMarker / InvMarkerCollide（否则标记菜单会把它当玩家标记处理），按分类染色。
//   - 定位：GetMapZoneFromSceneName → GetSceneInfo → GetMapPosition（游戏放尸体标记同款）。
//   - 显隐：全局 pin 开关 != None && 场景已绘制 && 未收集（map_collected.hpp）。
//
// 托管对象全部"现取现用"，不跨帧持有托管指针（原生持有的引用 GC 看不见）：
// 我们的标记在 markerParent 下是连续一段子节点，每次刷新 Find("sshud_pin_0")
// 取起点，其余按下标 GetChild —— O(n)，几百个标记也不会卡开图。

#pragma once

#include "lib.hpp"
#include "map_collected.hpp"
#include "map_markers_data.hpp"
#include "unity_api.hpp"

#if __has_include("collectibles.gen.h")
#include "collectibles.gen.h"
#define SSHUD_HAS_COLLECTIBLES 1
#else
#define SSHUD_HAS_COLLECTIBLES 0
#endif

#include <cstdio>
#include <cstring>

namespace sshud::map {

struct Vector2 {
  float x;
  float y;
};

constexpr int32_t PIN_STATE_NONE = 2;          // MapPin.PinVisibilityStates.None
constexpr uintptr_t IL2CPP_ARRAY_LENGTH = 0x18;
constexpr uintptr_t IL2CPP_ARRAY_ITEMS = 0x20;
constexpr float NO_MAP_POSITION = -1000.0f;    // GetMapPosition 在场景缺地图块时的返回值
constexpr size_t MARKER_NAME_CAPACITY = 32;
constexpr const char* FIRST_MARKER_NAME = "sshud_pin_0";
constexpr int SCENE_CACHE_SIZE = 64;

// ---------------------------------------------------------------- 托管调用
template <typename R, typename... A>
R call(uintptr_t rva, A... args) {
  return ui::mainFunction<R (*)(A..., const void*)>(rva)(args..., nullptr);
}

inline ui::ManagedObject transformOf(ui::ManagedObject go) { return call<ui::ManagedObject>(GO_GETTRANSFORM, go); }
inline ui::ManagedObject gameObjectOf(ui::ManagedObject c) { return call<ui::ManagedObject>(COMP_GETGAMEOBJECT, c); }
inline void setActive(ui::ManagedObject go, bool on) { call<void>(GO_SETACTIVE, go, on); }
inline void destroyObject(ui::ManagedObject o) { call<void>(UOBJ_DESTROY, o); }
inline int32_t pinVisibilityState() { return ui::mainFunction<int32_t (*)(const void*)>(MAPPIN_GETSTATE)(nullptr); }

inline ui::ManagedObject findChild(ui::ManagedObject tr, const char* name) {
  return call<ui::ManagedObject>(TR_FIND, tr, managedString(name));
}
inline ui::ManagedObject childAt(ui::ManagedObject tr, int32_t i) { return call<ui::ManagedObject>(TR_GETCHILD, tr, i); }
inline int32_t childCount(ui::ManagedObject tr) { return call<int32_t>(TR_GETCHILDCOUNT, tr); }
inline int32_t siblingIndex(ui::ManagedObject tr) { return call<int32_t>(TR_GETSIBLINGINDEX, tr); }
inline ui::ManagedObject componentByName(ui::ManagedObject go, const char* type) {
  return call<ui::ManagedObject>(GO_GETCOMPONENT_STR, go, managedString(type));
}

// ---------------------------------------------------------------- 场景 → 地图坐标
struct SceneInfo {
  const char* name = nullptr;
  ui::ManagedObject scene = nullptr;     // GameMapScene
  ui::ManagedObject sceneObj = nullptr;  // GameObject
  Vector2 scenePos{};
  bool isMapped = false;
};

inline SceneInfo querySceneInfo(ui::ManagedObject gameMap, const char* sceneName) {
  const ui::ManagedString name = managedString(sceneName);
  const int32_t zone = call<int32_t>(GM_GETMAPZONEFROMSCENE, gameMap, name);
  SceneInfo info;
  info.name = sceneName;
  call<void>(GM_GETSCENEINFO, gameMap, name, zone, &info.scene, &info.sceneObj, &info.scenePos);
  info.isMapped = ui::isAlive(info.scene) && call<bool>(GMS_ISMAPPED, info.scene);
  return info;
}

/// 一次刷新内的场景信息缓存（同一场景的多个收集物只查一次）
class SceneCache {
 public:
  const SceneInfo& get(ui::ManagedObject gameMap, const char* sceneName) {
    for (int i = 0; i < count_; ++i) {
      if (std::strcmp(entries_[i].name, sceneName) == 0) return entries_[i];
    }
    SceneInfo& slot = entries_[next_];
    slot = querySceneInfo(gameMap, sceneName);
    next_ = (next_ + 1) % SCENE_CACHE_SIZE;
    count_ = count_ < SCENE_CACHE_SIZE ? count_ + 1 : count_;
    return slot;
  }

 private:
  SceneInfo entries_[SCENE_CACHE_SIZE];
  int count_ = 0;
  int next_ = 0;
};

inline Vector2 mapPositionOf(ui::ManagedObject gameMap, const CollectibleEntry& entry, const SceneInfo& info) {
  if (!ui::isAlive(info.scene)) return {NO_MAP_POSITION, NO_MAP_POSITION};
  return call<Vector2>(GM_GETMAPPOSITION, gameMap, Vector2{entry.worldX, entry.worldY}, info.scene, info.sceneObj,
                       info.scenePos, Vector2{entry.sceneWidth, entry.sceneHeight});
}

// ---------------------------------------------------------------- 标记的建立
inline ui::ManagedObject markerTemplate(ui::ManagedObject gameMap) {
  const void* templates = ui::readField<const void*>(gameMap, GM_MARKERTEMPLATES);
  if (templates == nullptr || ui::readField<uint64_t>(templates, IL2CPP_ARRAY_LENGTH) == 0) return nullptr;
  return ui::readField<ui::ManagedObject>(templates, IL2CPP_ARRAY_ITEMS);
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
  ui::ManagedObject renderer = componentByName(marker, "SpriteRenderer");
  const Tint tint = tintFor(kind);
  if (ui::isAlive(renderer)) call<void>(SR_SETCOLOR, renderer, ui::Color{tint.r, tint.g, tint.b, tint.a});
  return marker;
}

// ---------------------------------------------------------------- 刷新
struct RefreshStats {
  int created = 0;
  int unplaceable = 0;
  int unmapped = 0;
  int collected = 0;
  int visible = 0;
};

inline void updateMarker(ui::ManagedObject gameMap, ui::ManagedObject marker, const CollectibleEntry& entry,
                         bool pinsEnabled, SceneCache& scenes, RefreshStats& stats) {
  const SceneInfo& info = scenes.get(gameMap, entry.scene);
  const Vector2 pos = mapPositionOf(gameMap, entry, info);
  bool isVisible = pinsEnabled;
  if (pos.x <= NO_MAP_POSITION) {
    ++stats.unplaceable;
    isVisible = false;
  } else {
    ui::ManagedObject transform = transformOf(marker);
    const ui::Vector3 current = call<ui::Vector3>(TR_GETLOCALPOSITION, transform);
    call<void>(TR_SETLOCALPOSITION, transform, ui::Vector3{pos.x, pos.y, current.z});
    if (!info.isMapped) {
      ++stats.unmapped;
      isVisible = false;
    } else if (isCollected(entry)) {
      ++stats.collected;
      isVisible = false;
    }
  }
  setActive(marker, isVisible);
  stats.visible += isVisible ? 1 : 0;
}

#if SSHUD_HAS_COLLECTIBLES
constexpr size_t COLLECTIBLE_COUNT = sizeof(COLLECTIBLES) / sizeof(COLLECTIBLES[0]);

/// 我们的标记在 parent 下的起始下标；还没建过（或数量对不上）返回 -1
inline int32_t firstMarkerIndex(ui::ManagedObject parent) {
  ui::ManagedObject first = findChild(parent, FIRST_MARKER_NAME);
  if (!ui::isAlive(first)) return -1;
  const int32_t start = siblingIndex(first);
  return childCount(parent) - start >= static_cast<int32_t>(COLLECTIBLE_COUNT) ? start : -1;
}

inline void refreshMarkers(ui::ManagedObject gameMap) {
  ui::ManagedObject parent = ui::readField<ui::ManagedObject>(gameMap, GM_MARKERPARENT);
  ui::ManagedObject tpl = markerTemplate(gameMap);
  if (!ui::isAlive(parent) || !ui::isAlive(tpl)) {
    Logging.Log("[sshud] map: no marker template/parent (tpl=%p parent=%p)", tpl, parent);
    return;
  }
  const int32_t start = firstMarkerIndex(parent);
  const bool pinsEnabled = pinVisibilityState() != PIN_STATE_NONE;
  RefreshStats stats;
  static SceneCache s_scenes;  // 大对象不放栈上；每次刷新重置
  s_scenes = SceneCache{};
  for (size_t i = 0; i < COLLECTIBLE_COUNT; ++i) {
    ui::ManagedObject marker = start >= 0 ? gameObjectOf(childAt(parent, start + static_cast<int32_t>(i)))
                                          : createMarker(tpl, parent, i, COLLECTIBLES[i].kind);
    if (marker == nullptr) continue;
    stats.created += start >= 0 ? 0 : 1;
    updateMarker(gameMap, marker, COLLECTIBLES[i], pinsEnabled, s_scenes, stats);
  }
  Logging.Log("[sshud] map: pins=%d created=%d visible=%d collected=%d unmapped=%d unplaceable=%d of %zu",
              pinsEnabled ? 1 : 0, stats.created, stats.visible, stats.collected, stats.unmapped, stats.unplaceable,
              COLLECTIBLE_COUNT);
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
#endif

namespace sshud::map {

inline void installHooks() {
#if SSHUD_HAS_COLLECTIBLES
  SetupMapMarkersHook::InstallAtOffset(GM_SETUPMAPMARKERS);
  Logging.Log("[sshud] map hooks installed: %zu collectibles", COLLECTIBLE_COUNT);
#else
  Logging.Log("[sshud] map: collectibles.gen.h missing, map markers disabled");
#endif
}

}  // namespace sshud::map
