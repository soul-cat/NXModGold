// exl/program/map_refresh_plan.hpp 的宿主机单测
#include "../exl/program/map_refresh_plan.hpp"

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

/// 模拟一组标记在挂钩之间的状态变化（Create / Full 之后标记存在，Hide 不建标记）
struct Simulation {
  bool hasMarkers = false;
  MarkerMode mode = MarkerMode::Unset;

  RefreshAction run(RefreshHook hook, bool isEnabled) {
    const RefreshAction action = decideRefresh({hook, isEnabled, hasMarkers, mode});
    if (action == RefreshAction::Create || action == RefreshAction::Full) hasMarkers = true;
    mode = modeAfter(action, mode);
    return action;
  }
};

constexpr RefreshHook SETUP = RefreshHook::SetupMapMarkers;
constexpr RefreshHook ENABLE = RefreshHook::EnableUnlockedAreas;

static void testWorldMapOrder() {
  std::printf("test: 大地图 SetupMapMarkers → EnableUnlockedAreas，每次开图只完整刷新一次\n");
  Simulation sim;
  CHECK(sim.run(SETUP, true) == RefreshAction::Create);
  CHECK(sim.run(ENABLE, true) == RefreshAction::Full);
  for (int open = 0; open < 3; ++open) {
    CHECK(sim.run(SETUP, true) == RefreshAction::Skip);
    CHECK(sim.run(ENABLE, true) == RefreshAction::Full);
  }
}

static void testQuickMapOrder() {
  std::printf("test: 快速地图 EnableUnlockedAreas → SetupMapMarkers，后者跳过\n");
  Simulation sim;
  CHECK(sim.run(ENABLE, true) == RefreshAction::Full);
  CHECK(sim.run(SETUP, true) == RefreshAction::Skip);
  CHECK(sim.run(ENABLE, true) == RefreshAction::Full);
  CHECK(sim.run(SETUP, true) == RefreshAction::Skip);
}

static void testMarkerMenuAfterOpen() {
  std::printf("test: 地图开着时放 / 删手动标记（只调 SetupMapMarkers）不刷新\n");
  Simulation sim;
  sim.run(SETUP, true);
  sim.run(ENABLE, true);
  CHECK(sim.run(SETUP, true) == RefreshAction::Skip);
  CHECK(sim.run(SETUP, true) == RefreshAction::Skip);
}

static void testDisabledHides() {
  std::printf("test: 关闭时两个挂钩都只隐藏\n");
  Simulation sim;
  sim.run(ENABLE, true);
  CHECK(sim.run(SETUP, false) == RefreshAction::Hide);
  CHECK(sim.run(ENABLE, false) == RefreshAction::Hide);
  CHECK(sim.mode == MarkerMode::Hidden);
}

static void testReenabledRefreshesOnSetup() {
  std::printf("test: 被开关隐藏后重新开启，SetupMapMarkers 也完整刷新（放标记时没有 EnableUnlockedAreas 跟上）\n");
  Simulation sim;
  sim.run(ENABLE, true);
  sim.run(SETUP, false);
  CHECK(sim.run(SETUP, true) == RefreshAction::Full);
  CHECK(sim.run(SETUP, true) == RefreshAction::Skip);
}

static void testCreatedButNotRefreshed() {
  std::printf("test: 只建未刷新时 SetupMapMarkers 完整刷新\n");
  Simulation sim;
  CHECK(sim.run(SETUP, true) == RefreshAction::Create);
  CHECK(sim.mode == MarkerMode::Unset);
  CHECK(sim.run(SETUP, true) == RefreshAction::Full);
  CHECK(sim.mode == MarkerMode::Shown);
}

static void testMarkersLostRebuilds() {
  std::printf("test: 标记被销毁（换了 GameMap）后 SetupMapMarkers 重建\n");
  Simulation sim;
  sim.run(ENABLE, true);
  sim.hasMarkers = false;
  CHECK(sim.run(SETUP, true) == RefreshAction::Create);
  CHECK(sim.run(ENABLE, true) == RefreshAction::Full);
}

static void testSetActiveOnlyOnChange() {
  std::printf("test: SetActive 只在状态变化时调用\n");
  CHECK(needsSetActive(ActiveState::Unknown, false));
  CHECK(needsSetActive(ActiveState::Unknown, true));
  CHECK(!needsSetActive(ActiveState::On, true));
  CHECK(!needsSetActive(ActiveState::Off, false));
  CHECK(needsSetActive(ActiveState::On, false));
  CHECK(needsSetActive(ActiveState::Off, true));
  CHECK(activeStateOf(true) == ActiveState::On && activeStateOf(false) == ActiveState::Off);
}

int main() {
  std::printf("=== 地图刷新决策测试 ===\n");
  testWorldMapOrder();
  testQuickMapOrder();
  testMarkerMenuAfterOpen();
  testDisabledHides();
  testReenabledRefreshesOnSetup();
  testCreatedButNotRefreshed();
  testMarkersLostRebuilds();
  testSetActiveOnlyOnChange();
  if (g_failed) {
    std::printf("\n%d 项失败\n", g_failed);
    return EXIT_FAILURE;
  }
  std::printf("\n全部通过\n");
  return EXIT_SUCCESS;
}
