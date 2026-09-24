// exl/program/toggles_model.hpp 的宿主机单测
#include "../exl/program/toggles_model.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace sshud::toggles;

static int g_failed = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
      ++g_failed;                                                     \
    }                                                                 \
  } while (0)

static void testComboNeedsHold() {
  std::printf("test: 不按住 L+R 时方向键不触发\n");
  ComboDetector detector;
  CHECK(detector.update(BUTTON_UP) == Feature::None);
  CHECK(detector.update(0) == Feature::None);
  CHECK(detector.update(BUTTON_L | BUTTON_DOWN) == Feature::None);
}

static void testComboFiresOnPressEdge() {
  std::printf("test: 按住 L+R 后按下沿触发一次，按住不重复\n");
  ComboDetector detector;
  CHECK(detector.update(COMBO_HOLD) == Feature::None);
  CHECK(detector.update(COMBO_HOLD | BUTTON_UP) == Feature::Hud);
  CHECK(detector.update(COMBO_HOLD | BUTTON_UP) == Feature::None);
  CHECK(detector.update(COMBO_HOLD) == Feature::None);
  CHECK(detector.update(COMBO_HOLD | BUTTON_DOWN) == Feature::Map);
}

static void testComboPressedTogether() {
  std::printf("test: L、R、方向键同一帧按下也算\n");
  ComboDetector detector;
  CHECK(detector.update(COMBO_HOLD | BUTTON_DOWN) == Feature::Map);
}

static void testFlipAndToast() {
  std::printf("test: 切换翻转对应开关，提示显示 TOAST_SECONDS 秒\n");
  Toggles toggles;
  CHECK(!toggles.isToastVisible(0.0f));
  toggles.flip(Feature::Hud, 10.0f);
  CHECK(!toggles.isHudEnabled && toggles.isMapEnabled);
  CHECK(std::strcmp(toggles.toastText(), "HP bars: OFF") == 0);
  CHECK(toggles.isToastVisible(10.0f + TOAST_SECONDS - 0.1f));
  CHECK(!toggles.isToastVisible(10.0f + TOAST_SECONDS));
  toggles.flip(Feature::Map, 20.0f);
  CHECK(!toggles.isMapEnabled);
  CHECK(std::strcmp(toggles.toastText(), "Map markers: OFF (reopen map)") == 0);
  toggles.flip(Feature::Hud, 21.0f);
  CHECK(toggles.isHudEnabled && std::strcmp(toggles.toastText(), "HP bars: ON") == 0);
  toggles.flip(Feature::None, 22.0f);
  CHECK(toggles.isHudEnabled && toggles.toastUntil == 21.0f + TOAST_SECONDS);
}

int main() {
  std::printf("=== 组合键开关测试 ===\n");
  testComboNeedsHold();
  testComboFiresOnPressEdge();
  testComboPressedTogether();
  testFlipAndToast();
  if (g_failed) {
    std::printf("\n%d 项失败\n", g_failed);
    return EXIT_FAILURE;
  }
  std::printf("\n全部通过\n");
  return EXIT_SUCCESS;
}
