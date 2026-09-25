// 地图刷新耗时统计（纯逻辑，tests/test_map_timing.cpp 覆盖）。
// 每次完整刷新记一次耗时，攒满 TIMING_LOG_INTERVAL 次才写一行平均 / 最大值，
// 避免每次开图都写 SD 卡（每写一行都要开文件、写入、flush、关文件，本身就会卡）。

#pragma once

#include <algorithm>
#include <cstdint>

namespace sshud::map {

constexpr int TIMING_LOG_INTERVAL = 8;  // 只记完整刷新（每次开图一次），约 8 次开图写一行

struct RefreshTiming {
  int count = 0;
  uint64_t totalMicros = 0;
  uint64_t maxMicros = 0;

  /// 记一次耗时；攒满 TIMING_LOG_INTERVAL 次返回 true，调用方写完日志后 reset()
  bool add(uint64_t micros) {
    ++count;
    totalMicros += micros;
    maxMicros = std::max(maxMicros, micros);
    return count >= TIMING_LOG_INTERVAL;
  }

  uint64_t averageMicros() const { return count == 0 ? 0 : totalMicros / static_cast<uint64_t>(count); }

  void reset() { *this = RefreshTiming{}; }
};

}  // namespace sshud::map
