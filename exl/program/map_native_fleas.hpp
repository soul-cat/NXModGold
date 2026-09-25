// 游戏原生的跳蚤图钉：跳蚤商队的猎人按区域出售（PlayerData.hasPinFlea*），买了的区域游戏自己在地图上标跳蚤，
// 我们的跳蚤标记再画一遍就重复了。这里收集已买区域的原生图钉，跳蚤标记与其中之一对上就隐藏。
//
// 对上的判据（实机日志：29 个图钉的 GameObject 名就是跳蚤的存档判定键，如 SavedFlea_Crawl_06 / tamedGiantFlea；
// MapPin.parentScene 全为空，不可用）：
//   1. 图钉名 = 我们条目的 persistentId；
//   2. 名字对不上的（如 CaravanLechSaved 对 CaravanLechReturnedToCaravan）看世界坐标距离 < NATIVE_FLEA_MATCH_DISTANCE
//      （图钉之间最近约 1 个单位）。
// 每组图钉第一次收集时写进日志，没对上的跳蚤写一次离最近图钉的距离。

#pragma once

#include "map_calls.hpp"
#include "options_menu_calls.hpp"

#include <cmath>
#include <cstring>

namespace sshud::map {

constexpr size_t NATIVE_FLEA_GROUP_COUNT = 6;  // CaravanTroupeHunter.PinGroups
constexpr uintptr_t HAS_PIN_FLEA_OFFSETS[NATIVE_FLEA_GROUP_COUNT] = {
    PD_HASPINFLEA_MARROWLANDS, PD_HASPINFLEA_MIDLANDS,  PD_HASPINFLEA_BLASTEDLANDS,
    PD_HASPINFLEA_CITADEL,     PD_HASPINFLEA_PEAKLANDS, PD_HASPINFLEA_MUCKLANDS,
};
constexpr size_t MAX_NATIVE_FLEA_PINS = 64;
constexpr size_t PIN_NAME_CAPACITY = 40;
constexpr float NATIVE_FLEA_MATCH_DISTANCE = 0.5f;  // 地图世界坐标

struct NativeFleaPin {
  ui::Vector3 position;
  char name[PIN_NAME_CAPACITY];
};

inline float distanceBetween(ui::Vector3 a, ui::Vector3 b) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  return std::sqrt(dx * dx + dy * dy);
}

/// 本次开图时已买区域的原生跳蚤图钉（纯原生数据，可放栈上）
struct NativeFleaPins {
  NativeFleaPin pins[MAX_NATIVE_FLEA_PINS];
  size_t count = 0;

  /// 最近的原生图钉距离；名字就是 persistentId 的直接返回 0
  float nearest(const char* persistentId, ui::Vector3 marker) const {
    float best = INFINITY;
    for (size_t i = 0; i < count; ++i) {
      if (std::strcmp(pins[i].name, persistentId) == 0) return 0.0f;
      best = std::fmin(best, distanceBetween(pins[i].position, marker));
    }
    return best;
  }
};

inline bool hasBoughtFleaGroup(size_t group) {
  const void* playerData = call<const void*>(PD_GETINSTANCE);
  return playerData != nullptr && ui::readField<bool>(playerData, HAS_PIN_FLEA_OFFSETS[group]);
}

inline void addNativePin(NativeFleaPins& out, ui::ManagedObject pinTr) {
  if (out.count >= MAX_NATIVE_FLEA_PINS) return;
  NativeFleaPin& pin = out.pins[out.count++];
  pin.position = call<ui::Vector3>(TR_GETPOSITION, pinTr);
  options::nameOf(pinTr, pin.name, sizeof(pin.name));
}

/// 每组第一次收集时把图钉结构写进日志
inline void logGroupOnce(size_t group, const NativeFleaPins& pins, size_t first) {
  static bool s_logged[NATIVE_FLEA_GROUP_COUNT] = {};
  if (s_logged[group]) return;
  s_logged[group] = true;
  Logging.Log("[sshud] map: native flea group %zu has %zu pins", group, pins.count - first);
  for (size_t i = first; i < pins.count; ++i) {
    const NativeFleaPin& pin = pins.pins[i];
    Logging.Log("[sshud] map:   pin %s pos=(%.2f,%.2f)", pin.name, pin.position.x, pin.position.y);
  }
}

inline NativeFleaPins collectNativeFleaPins(ui::ManagedObject gameMap) {
  NativeFleaPins pins;
  const void* parents = ui::readField<const void*>(gameMap, GM_FLEAPINPARENTS);
  if (parents == nullptr) return pins;
  const uint64_t groups = std::min<uint64_t>(arrayLength(parents), NATIVE_FLEA_GROUP_COUNT);
  for (uint64_t g = 0; g < groups; ++g) {
    ui::ManagedObject parent = arrayItem(parents, g);
    if (!hasBoughtFleaGroup(g) || !ui::isAlive(parent)) continue;
    const size_t first = pins.count;
    const int32_t children = childCount(parent);
    for (int32_t i = 0; i < children; ++i) addNativePin(pins, childAt(parent, i));
    logGroupOnce(g, pins, first);
  }
  return pins;
}

constexpr size_t MAX_LOGGED_FLEA_INDEX = 1024;

/// 没对上的跳蚤每个只写一次最近距离（校准阈值用）；index 是收集物下标
inline void logUnmatchedFleaOnce(size_t index, const char* scene, float distance) {
  static bool s_logged[MAX_LOGGED_FLEA_INDEX] = {};
  if (index >= MAX_LOGGED_FLEA_INDEX || s_logged[index]) return;
  s_logged[index] = true;
  Logging.Log("[sshud] map: flea %s nearest native pin %.2f", scene, distance);
}

}  // namespace sshud::map
