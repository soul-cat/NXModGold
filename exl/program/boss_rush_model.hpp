// BOSS 挑战菜单的纯逻辑：开关、翻页、选择，输入 → 命令（不依赖 Unity / nn，tests/test_boss_rush_model.cpp）。
//
// 按住 R3 再按 ZR 打开（R3 + 方向键是快捷面板）；打开后 上/下 选、左/右 换幕、A 执行、B 或再按 R3+ZR 关。
// 每页开头两项固定：回休息点、回满状态；之后是这一幕的 BOSS（BOSSES 里 act 相同的，按表内顺序）。
// 不能用的时候（不在 4 号位、没有角色、切场景中）不响应，已打开的也会关掉。

#pragma once

#include "boss_rush_table.hpp"

#include <cstdint>

namespace sshud::bossrush {

// nn::hid::NpadButton 的位
constexpr uint64_t BUTTON_A = 1ull << 0;
constexpr uint64_t BUTTON_B = 1ull << 1;
constexpr uint64_t BUTTON_STICK_R = 1ull << 5;
constexpr uint64_t BUTTON_ZR = 1ull << 9;
constexpr uint64_t BUTTON_LEFT = 1ull << 12;
constexpr uint64_t BUTTON_UP = 1ull << 13;
constexpr uint64_t BUTTON_RIGHT = 1ull << 14;
constexpr uint64_t BUTTON_DOWN = 1ull << 15;

constexpr int FIXED_ITEM_COUNT = 2;  // 回休息点、回满状态
constexpr int ITEM_REST = 0;
constexpr int ITEM_REFILL = 1;

enum class Command { None, Opened, Closed, Rest, Refill, Travel };

struct MenuResult {
  Command command = Command::None;
  int bossIndex = -1;  // Travel 时是 BOSSES 下标
};

/// act（1..ACT_COUNT）的第 slot 个 BOSS 在 BOSSES 里的下标；没有返回 -1
inline int bossIndexInAct(int act, int slot) {
  int seen = 0;
  for (size_t i = 0; i < BOSS_COUNT; ++i) {
    if (BOSSES[i].act != act) continue;
    if (seen == slot) return static_cast<int>(i);
    ++seen;
  }
  return -1;
}

inline int bossCountInAct(int act) {
  int count = 0;
  for (size_t i = 0; i < BOSS_COUNT; ++i) count += BOSSES[i].act == act ? 1 : 0;
  return count;
}

class BossMenu {
 public:
  bool isOpen() const { return isOpen_; }
  int act() const { return act_; }
  int cursor() const { return cursor_; }
  int itemCount() const { return FIXED_ITEM_COUNT + bossCountInAct(act_); }

  /// 每帧调用：buttons 是当前按住的键；canUse = 4 号位且有角色、不在切场景
  MenuResult update(uint64_t buttons, bool canUse) {
    const uint64_t pressed = buttons & ~previous_;
    previous_ = buttons;
    const bool isComboPressed = (buttons & BUTTON_STICK_R) != 0 && (pressed & BUTTON_ZR) != 0;
    if (!isOpen_) {
      if (!canUse || !isComboPressed) return {};
      isOpen_ = true;
      cursor_ = 0;
      return {Command::Opened};
    }
    if (!canUse || isComboPressed || (pressed & BUTTON_B) != 0) return close();
    if ((pressed & BUTTON_A) != 0) return confirm();
    moveCursor(pressed);
    return {};
  }

 private:
  MenuResult close() {
    isOpen_ = false;
    return {Command::Closed};
  }

  MenuResult confirm() {
    isOpen_ = false;
    if (cursor_ == ITEM_REST) return {Command::Rest};
    if (cursor_ == ITEM_REFILL) return {Command::Refill};
    return {Command::Travel, bossIndexInAct(act_, cursor_ - FIXED_ITEM_COUNT)};
  }

  void moveCursor(uint64_t pressed) {
    const int count = itemCount();
    if ((pressed & BUTTON_DOWN) != 0) cursor_ = (cursor_ + 1) % count;
    if ((pressed & BUTTON_UP) != 0) cursor_ = (cursor_ + count - 1) % count;
    if ((pressed & BUTTON_RIGHT) != 0) act_ = act_ % ACT_COUNT + 1;
    if ((pressed & BUTTON_LEFT) != 0) act_ = (act_ + ACT_COUNT - 2) % ACT_COUNT + 1;
    if (cursor_ >= itemCount()) cursor_ = itemCount() - 1;
  }

  uint64_t previous_ = 0;
  bool isOpen_ = false;
  int act_ = 1;
  int cursor_ = 0;
};

}  // namespace sshud::bossrush
