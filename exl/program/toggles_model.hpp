// 组合键开关的纯逻辑（不依赖 Unity / nn，tests/test_toggles.cpp 覆盖）。
//
// 按住 L + R，再按 ↑ 切血条（含伤害数字、BOSS 条），再按 ↓ 切地图收集物标记。
// 地图标记在下次打开地图时生效（标记只在 GameMap.SetupMapMarkers 时刷新）。

#pragma once

#include <cstdint>

namespace sshud::toggles {

// nn::hid::NpadButton 的位
constexpr uint64_t BUTTON_L = 1ull << 6;
constexpr uint64_t BUTTON_R = 1ull << 7;
constexpr uint64_t BUTTON_UP = 1ull << 13;
constexpr uint64_t BUTTON_DOWN = 1ull << 15;

constexpr uint64_t COMBO_HOLD = BUTTON_L | BUTTON_R;
constexpr uint64_t COMBO_HUD = BUTTON_UP;
constexpr uint64_t COMBO_MAP = BUTTON_DOWN;

constexpr float TOAST_SECONDS = 2.0f;

enum class Feature : uint8_t { None, Hud, Map };

/// 每帧喂一次当前按键；返回本帧触发的开关（只认按下沿，按住不重复触发）
class ComboDetector {
 public:
  Feature update(uint64_t buttons) {
    const uint64_t pressed = buttons & ~previous_;
    previous_ = buttons;
    if ((buttons & COMBO_HOLD) != COMBO_HOLD) return Feature::None;
    if ((pressed & COMBO_HUD) != 0) return Feature::Hud;
    if ((pressed & COMBO_MAP) != 0) return Feature::Map;
    return Feature::None;
  }

 private:
  uint64_t previous_ = 0;
};

struct Toggles {
  bool isHudEnabled = true;
  bool isMapEnabled = true;
  Feature lastChanged = Feature::None;
  float toastUntil = 0.0f;

  void flip(Feature feature, float now) {
    if (feature == Feature::None) return;
    bool& flag = feature == Feature::Hud ? isHudEnabled : isMapEnabled;
    flag = !flag;
    lastChanged = feature;
    toastUntil = now + TOAST_SECONDS;
  }

  bool isToastVisible(float now) const { return lastChanged != Feature::None && now < toastUntil; }

  /// 提示文字用英文：IMGUI 默认字体不保证有中文字形
  const char* toastText() const {
    if (lastChanged == Feature::Hud) return isHudEnabled ? "HP bars: ON" : "HP bars: OFF";
    return isMapEnabled ? "Map markers: ON (reopen map)" : "Map markers: OFF (reopen map)";
  }
};

}  // namespace sshud::toggles
