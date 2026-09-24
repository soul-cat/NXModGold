// 组合键开关的运行时部分：每帧读手柄（nn::hid，游戏的 sdk 模块导出），喂给 ComboDetector。
//
// 读三种握法再按位或：Pro 手柄 / 模拟器键盘（FullKey, No1）、双 Joy-Con（JoyDual, No1）、
// 掌机（Handheld）。没连上的握法读出来全 0，不影响结果。只读状态，不会吞掉游戏的输入。

#pragma once

#include "lib.hpp"
#include "toggles_model.hpp"

#include <cstring>

namespace nn::hid {

struct NpadFullKeyState;
struct NpadJoyDualState;
struct NpadHandheldState;

void GetNpadState(NpadFullKeyState* out, const unsigned int& id);
void GetNpadState(NpadJoyDualState* out, const unsigned int& id);
void GetNpadState(NpadHandheldState* out, const unsigned int& id);

}  // namespace nn::hid

namespace sshud::toggles {

constexpr unsigned int NPAD_ID_NO1 = 0;
constexpr unsigned int NPAD_ID_HANDHELD = 0x20;
constexpr size_t NPAD_STATE_BUFFER_SIZE = 0x80;  // NpadXxxState 实际 0x28，留余量防 SDK 加字段
constexpr size_t NPAD_BUTTONS_OFFSET = 0x8;      // int64 samplingNumber 之后是 NpadButtonSet

inline Toggles g_toggles;

template <typename State>
uint64_t buttonsOf(unsigned int id) {
  alignas(8) uint8_t buffer[NPAD_STATE_BUFFER_SIZE] = {};
  nn::hid::GetNpadState(reinterpret_cast<State*>(buffer), id);
  uint64_t buttons = 0;
  std::memcpy(&buttons, buffer + NPAD_BUTTONS_OFFSET, sizeof(buttons));
  return buttons;
}

inline uint64_t currentButtons() {
  return buttonsOf<nn::hid::NpadFullKeyState>(NPAD_ID_NO1) | buttonsOf<nn::hid::NpadJoyDualState>(NPAD_ID_NO1) |
         buttonsOf<nn::hid::NpadHandheldState>(NPAD_ID_HANDHELD);
}

/// 每帧调用一次（CameraController.LateUpdate）
inline void pollInput(float now) {
  static ComboDetector s_detector;
  const Feature feature = s_detector.update(currentButtons());
  if (feature == Feature::None) return;
  g_toggles.flip(feature, now);
  Logging.Log("[sshud] toggle: hud=%d map=%d", g_toggles.isHudEnabled ? 1 : 0, g_toggles.isMapEnabled ? 1 : 0);
}

}  // namespace sshud::toggles
