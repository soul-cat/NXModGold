// exl/program/map_timing.hpp 的宿主机单测
#include "../exl/program/map_timing.hpp"

#include <cstdio>
#include <cstdlib>

using namespace sshud::map;

static int g_failed = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
      ++g_failed;                                                     \
    }                                                                 \
  } while (0)

static void testFiresEveryInterval() {
  std::printf("test: 攒满 TIMING_LOG_INTERVAL 次才返回 true\n");
  RefreshTiming timing;
  for (int i = 1; i < TIMING_LOG_INTERVAL; ++i) CHECK(!timing.add(100));
  CHECK(timing.add(100));
}

static void testAverageAndMax() {
  std::printf("test: 平均值与最大值\n");
  RefreshTiming timing;
  CHECK(timing.averageMicros() == 0);
  timing.add(1000);
  timing.add(3000);
  timing.add(2000);
  CHECK(timing.averageMicros() == 2000);
  CHECK(timing.maxMicros == 3000);
}

static void testResetStartsNewWindow() {
  std::printf("test: reset 后重新计数\n");
  RefreshTiming timing;
  for (int i = 0; i < TIMING_LOG_INTERVAL; ++i) timing.add(5000);
  timing.reset();
  CHECK(timing.count == 0 && timing.maxMicros == 0 && timing.averageMicros() == 0);
  CHECK(!timing.add(10));
  CHECK(timing.maxMicros == 10);
}

int main() {
  std::printf("=== 地图刷新耗时统计测试 ===\n");
  testFiresEveryInterval();
  testAverageAndMax();
  testResetStartsNewWindow();
  if (g_failed) {
    std::printf("\n%d 项失败\n", g_failed);
    return EXIT_FAILURE;
  }
  std::printf("\n全部通过\n");
  return EXIT_SUCCESS;
}
