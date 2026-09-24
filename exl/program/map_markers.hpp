// 地图收集物标记：没拿到就显示，拿到就隐藏（效果对齐游戏的跳蚤标记）。
//
// 做法照 Completionist Map（docs/tasks/research-collectible-map.md 4.1）：
//   - 刷新点：GameMap.SetupMapMarkers 之后（开大地图 / 快速地图都会调）。
//   - 建标记：克隆玩家手动标记模板 mapMarkerTemplates[0] 到 markerParent 下，
//     删掉 InvMarker / InvMarkerCollide（否则标记菜单会把它当玩家标记处理），按分类染色。
//   - 定位：GetMapZoneFromSceneName → GetSceneInfo → GetMapPosition（游戏放尸体标记同款）。
//   - 显隐：全局 pin 开关 != None && 场景已绘制 && SceneData 持久布尔为 false。
//
// 托管对象全部"现取现用"：每次刷新按名字 markerParent.Find("sshud_pin_N") 找回标记，
// 不跨帧持有任何托管指针（原生持有的引用 GC 看不见）。

#pragma once

#include "lib.hpp"
#include "map_markers_data.hpp"
#include "unity_api.hpp"

#if __has_include("collectibles.gen.h")
#include "collectibles.gen.h"
#define SSHUD_HAS_COLLECTIBLES 1
#else
#define SSHUD_HAS_COLLECTIBLES 0
#endif

#include <cstdio>

namespace sshud::map {

namespace ui = sshud::unity;

struct Vector2 {
  float x;
  float y;
};

constexpr int32_t PIN_STATE_NONE = 2;          // MapPin.PinVisibilityStates.None
constexpr uintptr_t IL2CPP_ARRAY_LENGTH = 0x18;
constexpr uintptr_t IL2CPP_ARRAY_ITEMS = 0x20;
constexpr uintptr_t PERSISTENT_BOOL_VALUE = 0x21;  // SerializableBoolData.Value（反汇编核实）
constexpr float NO_MAP_POSITION = -1000.0f;        // GetMapPosition 在场景缺地图块时的返回值
constexpr size_t MARKER_NAME_CAPACITY = 32;

// ---------------------------------------------------------------- 托管调用
inline ui::ManagedString managedString(const char* text) {
  return ui::mainFunction<ui::ManagedString (*)(const char*, const void*)>(MARSHAL_PTRTOSTRANSI)(text, nullptr);
}

inline ui::ManagedObject findChild(ui::ManagedObject transform, const char* name) {
  return ui::mainFunction<ui::ManagedObject (*)(ui::ManagedObject, ui::ManagedString, const void*)>(TR_FIND)(
      transform, managedString(name), nullptr);
}

inline ui::ManagedObject gameObjectOf(ui::ManagedObject component) {
  return ui::mainFunction<ui::ManagedObject (*)(ui::ManagedObject, const void*)>(COMP_GETGAMEOBJECT)(component,
                                                                                                  nullptr);
}

inline ui::ManagedObject transformOf(ui::ManagedObject gameObject) {
  return ui::mainFunction<ui::ManagedObject (*)(ui::ManagedObject, const void*)>(GO_GETTRANSFORM)(gameObject,
                                                                                               nullptr);
}

inline void setActive(ui::ManagedObject gameObject, bool isActive) {
  ui::mainFunction<void (*)(ui::ManagedObject, bool, const void*)>(GO_SETACTIVE)(gameObject, isActive, nullptr);
}

inline ui::ManagedObject componentByName(ui::ManagedObject gameObject, const char* typeName) {
  return ui::mainFunction<ui::ManagedObject (*)(ui::ManagedObject, ui::ManagedString, const void*)>(
      GO_GETCOMPONENT_STR)(gameObject, managedString(typeName), nullptr);
}

inline void destroyObject(ui::ManagedObject object) {
  ui::mainFunction<void (*)(ui::ManagedObject, const void*)>(UOBJ_DESTROY)(object, nullptr);
}

inline ui::ManagedObject instantiateUnder(ui::ManagedObject original, ui::ManagedObject parent) {
  return ui::mainFunction<ui::ManagedObject (*)(ui::ManagedObject, ui::ManagedObject, bool, const void*)>(
      UOBJ_INSTANTIATE)(original, parent, false, nullptr);
}

inline void setName(ui::ManagedObject object, const char* name) {
  ui::mainFunction<void (*)(ui::ManagedObject, ui::ManagedString, const void*)>(UOBJ_SETNAME)(
      object, managedString(name), nullptr);
}

inline ui::Vector3 localPosition(ui::ManagedObject transform) {
  return ui::mainFunction<ui::Vector3 (*)(ui::ManagedObject, const void*)>(TR_GETLOCALPOSITION)(transform, nullptr);
}

inline void setLocalPosition(ui::ManagedObject transform, ui::Vector3 position) {
  ui::mainFunction<void (*)(ui::ManagedObject, ui::Vector3, const void*)>(TR_SETLOCALPOSITION)(transform, position,
                                                                                            nullptr);
}

inline void setSpriteColor(ui::ManagedObject renderer, Tint tint) {
  ui::mainFunction<void (*)(ui::ManagedObject, ui::Color, const void*)>(SR_SETCOLOR)(
      renderer, {tint.r, tint.g, tint.b, tint.a}, nullptr);
}

inline int32_t pinVisibilityState() {
  return ui::mainFunction<int32_t (*)(const void*)>(MAPPIN_GETSTATE)(nullptr);
}

// ---------------------------------------------------------------- 场景 → 地图坐标
struct SceneInfo {
  ui::ManagedObject scene = nullptr;     // GameMapScene
  ui::ManagedObject sceneObj = nullptr;  // GameObject
  Vector2 scenePos{};
};

inline SceneInfo sceneInfoFor(ui::ManagedObject gameMap, const char* sceneName) {
  const ui::ManagedString name = managedString(sceneName);
  const int32_t zone = ui::mainFunction<int32_t (*)(ui::ManagedObject, ui::ManagedString, const void*)>(
      GM_GETMAPZONEFROMSCENE)(gameMap, name, nullptr);
  SceneInfo info;
  ui::mainFunction<void (*)(ui::ManagedObject, ui::ManagedString, int32_t, ui::ManagedObject*, ui::ManagedObject*,
                            Vector2*, const void*)>(GM_GETSCENEINFO)(gameMap, name, zone, &info.scene,
                                                                     &info.sceneObj, &info.scenePos, nullptr);
  return info;
}

inline Vector2 mapPositionOf(ui::ManagedObject gameMap, const CollectibleEntry& entry, const SceneInfo& info) {
  return ui::mainFunction<Vector2 (*)(ui::ManagedObject, Vector2, ui::ManagedObject, ui::ManagedObject, Vector2,
                                      Vector2, const void*)>(GM_GETMAPPOSITION)(
      gameMap, {entry.worldX, entry.worldY}, info.scene, info.sceneObj, info.scenePos,
      {entry.sceneWidth, entry.sceneHeight}, nullptr);
}

inline bool isSceneMapped(const SceneInfo& info) {
  if (!ui::isAlive(info.scene)) return false;
  return ui::mainFunction<bool (*)(ui::ManagedObject, const void*)>(GMS_ISMAPPED)(info.scene, nullptr);
}

// ---------------------------------------------------------------- 已收集判定
/// 共享泛型 TryGetValue 需要的 MethodInfo*：照游戏自己的写法先初始化 metadata 槽再取值
inline const void* tryGetValueMethodInfo() {
  auto* slot = reinterpret_cast<const void**>(exl::util::modules::GetTargetOffset(PDC_BOOL_TRYGETVALUE_MI));
  static bool s_initialized = false;
  if (!s_initialized) {
    ui::mainFunction<void (*)(const void*, bool)>(IL2CPP_INIT_METADATA)(slot, true);
    s_initialized = true;
  }
  return *slot;
}

/// SceneData.instance.persistentBools[scene, id].Value；查不到当作未收集
inline bool isCollected(const CollectibleEntry& entry) {
  const void* sceneData = ui::mainFunction<const void* (*)(const void*)>(SD_GETINSTANCE)(nullptr);
  if (sceneData == nullptr) return false;
  const void* collection = ui::readField<const void*>(sceneData, SD_PERSISTENTBOOLS);
  if (collection == nullptr) return false;
  const uint8_t* data = nullptr;
  const bool found = ui::mainFunction<bool (*)(const void*, ui::ManagedString, ui::ManagedString, const uint8_t**,
                                               const void*)>(PDC_BOOL_TRYGETVALUE)(
      collection, managedString(entry.scene), managedString(entry.persistentId), &data, tryGetValueMethodInfo());
  return found && data != nullptr && data[PERSISTENT_BOOL_VALUE] != 0;
}

// ---------------------------------------------------------------- 标记的建立与刷新
inline void markerName(char (&out)[MARKER_NAME_CAPACITY], size_t index) {
  std::snprintf(out, sizeof(out), "sshud_pin_%zu", index);
}

inline ui::ManagedObject markerTemplate(ui::ManagedObject gameMap) {
  const void* templates = ui::readField<const void*>(gameMap, GM_MARKERTEMPLATES);
  if (templates == nullptr || ui::readField<uint64_t>(templates, IL2CPP_ARRAY_LENGTH) == 0) return nullptr;
  return ui::readField<ui::ManagedObject>(templates, IL2CPP_ARRAY_ITEMS);
}

/// 新建一个标记；返回其 GameObject（失败返回 null）
inline ui::ManagedObject createMarker(ui::ManagedObject tpl, ui::ManagedObject parent, const char* name,
                                      CollectibleKind kind) {
  ui::ManagedObject marker = instantiateUnder(tpl, parent);
  if (marker == nullptr) return nullptr;
  setName(marker, name);
  for (const char* interactive : {"InvMarker", "InvMarkerCollide"}) {
    ui::ManagedObject component = componentByName(marker, interactive);
    if (ui::isAlive(component)) destroyObject(component);
  }
  ui::ManagedObject renderer = componentByName(marker, "SpriteRenderer");
  if (ui::isAlive(renderer)) setSpriteColor(renderer, tintFor(kind));
  return marker;
}

struct RefreshStats {
  int created = 0;
  int unplaceable = 0;
  int unmapped = 0;
  int collected = 0;
  int visible = 0;
};

/// 放置并决定一个标记是否显示
inline void updateMarker(ui::ManagedObject gameMap, ui::ManagedObject marker, const CollectibleEntry& entry,
                         bool pinsEnabled, RefreshStats& stats) {
  const SceneInfo info = sceneInfoFor(gameMap, entry.scene);
  const Vector2 pos = ui::isAlive(info.scene) ? mapPositionOf(gameMap, entry, info) : Vector2{NO_MAP_POSITION, 0};
  bool isVisible = pinsEnabled;
  if (pos.x <= NO_MAP_POSITION) {
    ++stats.unplaceable;
    isVisible = false;
  } else if (!isSceneMapped(info)) {
    ++stats.unmapped;
    isVisible = false;
  } else if (isCollected(entry)) {
    ++stats.collected;
    isVisible = false;
  }
  if (pos.x > NO_MAP_POSITION) {
    ui::ManagedObject transform = transformOf(marker);
    const ui::Vector3 current = localPosition(transform);
    setLocalPosition(transform, {pos.x, pos.y, current.z});
  }
  setActive(marker, isVisible);
  stats.visible += isVisible ? 1 : 0;
}

inline void refreshMarkers(ui::ManagedObject gameMap) {
#if SSHUD_HAS_COLLECTIBLES
  ui::ManagedObject parent = ui::readField<ui::ManagedObject>(gameMap, GM_MARKERPARENT);
  ui::ManagedObject tpl = markerTemplate(gameMap);
  if (!ui::isAlive(parent) || !ui::isAlive(tpl)) {
    Logging.Log("[sshud] map: no marker template/parent (tpl=%p parent=%p)", tpl, parent);
    return;
  }
  const bool pinsEnabled = pinVisibilityState() != PIN_STATE_NONE;
  RefreshStats stats;
  char name[MARKER_NAME_CAPACITY];
  for (size_t i = 0; i < sizeof(COLLECTIBLES) / sizeof(COLLECTIBLES[0]); ++i) {
    markerName(name, i);
    ui::ManagedObject found = findChild(parent, name);
    ui::ManagedObject marker = ui::isAlive(found) ? gameObjectOf(found) : nullptr;
    if (marker == nullptr) {
      marker = createMarker(tpl, parent, name, COLLECTIBLES[i].kind);
      if (marker == nullptr) continue;
      ++stats.created;
    }
    updateMarker(gameMap, marker, COLLECTIBLES[i], pinsEnabled, stats);
  }
  Logging.Log("[sshud] map: pins=%d created=%d visible=%d collected=%d unmapped=%d unplaceable=%d",
              pinsEnabled ? 1 : 0, stats.created, stats.visible, stats.collected, stats.unmapped,
              stats.unplaceable);
#else
  (void)gameMap;
#endif
}

}  // namespace sshud::map

HOOK_DEFINE_TRAMPOLINE(SetupMapMarkersHook) {
  static void Callback(void* self, const void* method) {
    Orig(self, method);
    if (sshud::unity::isAlive(self)) sshud::map::refreshMarkers(self);
  }
};

namespace sshud::map {

inline void installHooks() {
#if SSHUD_HAS_COLLECTIBLES
  SetupMapMarkersHook::InstallAtOffset(GM_SETUPMAPMARKERS);
  Logging.Log("[sshud] map hooks installed: %zu collectibles", sizeof(COLLECTIBLES) / sizeof(COLLECTIBLES[0]));
#else
  Logging.Log("[sshud] map: collectibles.gen.h missing, map markers disabled");
#endif
}

}  // namespace sshud::map
