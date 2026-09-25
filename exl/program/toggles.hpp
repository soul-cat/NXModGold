// 开关面板的运行时部分：每帧读手柄（nn::hid，游戏的 sdk 模块导出），喂给 MenuInput。
// 首次调用时从 SD 读开关（toggles_config）；松开 R3 时若有改动就写回 SD。
// 「R3 快捷面板」开关（Feature::QuickPanel，原生选项页里）关掉时整个面板停用。
//
// 读三种握法再按位或：Pro 手柄 / 模拟器键盘（FullKey, No1）、双 Joy-Con（JoyDual, No1）、
// 掌机（Handheld）。没连上的握法读出来全 0，不影响结果。只读状态，不会吞掉游戏的输入。

#pragma once

#include "lib.hpp"
#include "toggles_config.hpp"
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

inline Settings g_settings;
inline bool g_isMenuOpen = false;  // 本帧 R3 按住中：画开关面板
inline bool g_isDirty = false;     // 面板打开期间改过开关，松开 R3 时写回 SD

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

/// 首次调用时从 SD 读开关（调用方保证 SD 已挂载）
inline void loadOnce() {
  static bool s_isLoaded = false;
  if (s_isLoaded) return;
  s_isLoaded = true;
  loadSettings(g_settings);
}

/// 每帧调用一次（CameraController.LateUpdate，须在 sdlog::start() 之后）
inline void pollInput() {
  static MenuInput s_input;
  loadOnce();
  if (!g_settings.isEnabled(Feature::QuickPanel)) {
    g_isMenuOpen = false;
    s_input = MenuInput{};  // 重新打开时从干净状态开始，不把关着期间按住的键当成按下沿
    return;
  }
  const MenuEvent event = s_input.update(currentButtons());
  g_isMenuOpen = event.isOpen;
  if (event.flipped != Feature::None) {
    g_settings.flip(event.flipped);
    g_isDirty = true;
    Logging.Log("[sshud] toggle %s=%d", infoOf(event.flipped).key, g_settings.isEnabled(event.flipped) ? 1 : 0);
  }
  if (event.isClosed && g_isDirty) {
    saveSettings(g_settings);
    g_isDirty = false;
  }
}

}  // namespace sshud::toggles
