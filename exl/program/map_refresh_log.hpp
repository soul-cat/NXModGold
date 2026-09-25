// 地图刷新的统计与日志：计时（CNTPCT_EL0）、只在结果变化时写统计、耗时按批汇总（map_timing.hpp）。
// 每写一行 SD 日志都要开文件、写入、flush、关文件，每次开图都写本身就会卡。

#pragma once

#include "lib.hpp"
#include "map_calls.hpp"
#include "map_markers_data.hpp"
#include "map_timing.hpp"

#include <cstring>
#include <type_traits>

namespace sshud::map {

constexpr uint64_t SYSTEM_TICK_HZ = 19200000;  // Switch 的 CNTPCT_EL0 固定 19.2 MHz
constexpr uint64_t MICROS_PER_SECOND = 1000000;

inline uint64_t systemTick() {
  uint64_t tick = 0;
  asm volatile("mrs %0, cntpct_el0" : "=r"(tick));
  return tick;
}

inline uint64_t elapsedMicros(uint64_t startTick) {
  return (systemTick() - startTick) * MICROS_PER_SECOND / SYSTEM_TICK_HZ;
}

struct RefreshStats {
  int created = 0;
  int unplaceable = 0;
  int unmapped = 0;
  int collected = 0;
  int visible = 0;
  int iconless = 0;
  int noAreaMap = 0;
  int visibleByKind[COLLECTIBLE_KIND_COUNT] = {};
};
static_assert(std::has_unique_object_representations_v<RefreshStats>, "logRefresh 用 memcmp 比较，不能有填充字节");

inline void logStats(const RefreshStats& stats, uint64_t micros, size_t total) {
  Logging.Log("[sshud] map: created=%d visible=%d collected=%d unmapped=%d noAreaMap=%d unplaceable=%d "
              "iconless=%d of %zu (%llu us)",
              stats.created, stats.visible, stats.collected, stats.unmapped, stats.noAreaMap, stats.unplaceable,
              stats.iconless, total, static_cast<unsigned long long>(micros));
  for (size_t kind = 0; kind < COLLECTIBLE_KIND_COUNT; ++kind) {
    if (stats.visibleByKind[kind] > 0) {
      Logging.Log("[sshud] map: visible %s=%d", COLLECTIBLE_KIND_NAMES[kind], stats.visibleByKind[kind]);
    }
  }
}

/// 诊断：内存里的 PlayerData.mapAllRooms（为 true 时游戏无视区域地图、整张图全开）。只在值变化时写一行
inline void logMapAllRoomsChange() {
  static int s_lastValue = -1;
  const void* playerData = ui::mainFunction<const void* (*)(const void*)>(PD_GETINSTANCE)(nullptr);
  if (playerData == nullptr) return;
  const int value = ui::readField<bool>(playerData, PD_MAPALLROOMS) ? 1 : 0;
  if (value == s_lastValue) return;
  s_lastValue = value;
  Logging.Log("[sshud] map: PlayerData.mapAllRooms=%d", value);
}

/// 每次开图都写 SD 本身就会卡：统计只在结果变化时写（新建数不算变化），耗时攒够一批写一行汇总
inline void logRefresh(RefreshStats stats, uint64_t micros, size_t total) {
  static RefreshTiming s_timing;
  static RefreshStats s_lastLogged;
  static bool s_hasLogged = false;
  if (s_timing.add(micros)) {
    Logging.Log("[sshud] map: last %d refreshes avg %llu us, max %llu us", s_timing.count,
                static_cast<unsigned long long>(s_timing.averageMicros()),
                static_cast<unsigned long long>(s_timing.maxMicros));
    s_timing.reset();
  }
  const int created = stats.created;
  stats.created = 0;
  if (s_hasLogged && std::memcmp(&stats, &s_lastLogged, sizeof(stats)) == 0) return;
  s_lastLogged = stats;
  s_hasLogged = true;
  stats.created = created;
  logStats(stats, micros, total);
}

}  // namespace sshud::map
