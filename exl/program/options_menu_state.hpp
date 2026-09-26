// 注入到游戏选项页的行及其所在页面 / 导航列表；跨帧持有的对象都已钉住，可放静态区。

#pragma once

#include "options_menu_rows.hpp"

namespace sshud::options {

struct MenuState {
  ui::ManagedObject screen = nullptr;  // 注入时的游戏选项页（只比较，不解引用）
  ui::ManagedObject list = nullptr;
  ui::ManagedObject content = nullptr;  // 行所在的内容容器（排版用；页面活着它就活着）
  float maxSpacing = 0.0f;              // 注入前容器的原始行距（压缩行距的上限）
  ui::PinHandle listPin = 0;
  ui::ManagedObject originals[MAX_SCANNED_OPTIONS] = {};  // 原生选项行（只比较）
  size_t originalCount = 0;
  InjectedRow rows[ROW_COUNT];  // 顺序同 options_menu_model.hpp 的行（功能开关 + 起始纹章）
  int32_t onIndex = FALLBACK_ON_INDEX;
  bool isInjected = false;

  /// 注入过、页面没换、建成的行都还活着
  bool isIntact(ui::ManagedObject currentScreen) const {
    if (!isInjected || currentScreen != screen) return false;
    for (const InjectedRow& row : rows) {
      if (row.row != nullptr && !ui::isAlive(row.row)) return false;
    }
    return true;
  }

  void discard() {
    for (InjectedRow& row : rows) {
      if (ui::isAlive(row.row)) destroyNow(row.row);
      ui::unpinObject(row.rowPin);
      ui::unpinObject(row.optionPin);
      row = InjectedRow{};
    }
    ui::unpinObject(listPin);
    *this = MenuState{};
  }

  bool isOriginal(ui::ManagedObject selectable) const {
    for (size_t i = 0; i < originalCount; ++i) {
      if (originals[i] == selectable) return true;
    }
    return false;
  }
};

inline MenuState g_menu;

}  // namespace sshud::options
