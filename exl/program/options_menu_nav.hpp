// 把我们的行接进游戏选项页的手柄上下导航。
//
// 页面激活时 MenuButtonList.SetupActive 按 activeSelectables 重排导航，我们的行不在它的 entries 里，
// 所以在它之后把各行接到最后一个原生选项行与它原来的下一项之间（options_menu.hpp 的 hook 调用）。

#pragma once

#include "options_menu_state.hpp"

namespace sshud::options {

constexpr int32_t NAVIGATION_AUTOMATIC = 3;  // UnityEngine.UI.Navigation.Mode.Automatic

/// activeSelectables 里最后一个原生选项行
inline ui::ManagedObject lastOriginalSelectable(const MenuState& state) {
  const void* list = ui::readField<const void*>(state.list, MBL_ACTIVESELECTABLES);
  if (list == nullptr) return nullptr;
  const void* items = ui::readField<const void*>(list, LIST_ITEMS);
  const int32_t size = ui::readField<int32_t>(list, LIST_SIZE);
  for (int32_t i = size - 1; i >= 0 && items != nullptr; --i) {
    ui::ManagedObject selectable = map::arrayItem(items, static_cast<uint64_t>(i));
    if (state.isOriginal(selectable)) return selectable;
  }
  return nullptr;
}

/// 锚点不是显式导航时（不该发生）退回自动导航：让 Unity 按位置找上下邻居
inline void useAutomaticNavigation(const MenuState& state, int32_t anchorMode) {
  for (const InjectedRow& row : state.rows) {
    if (!ui::isAlive(row.option)) continue;
    Navigation nav = navigationOf(row.option);
    nav.mode = NAVIGATION_AUTOMATIC;
    setNavigation(row.option, nav);
  }
  Logging.Log("[sshud] options: anchor navigation mode=%d, rows use automatic", anchorMode);
}

/// 把建成的行串成链：anchor → 行… → next
inline ui::ManagedObject chainRows(const MenuState& state, ui::ManagedObject anchor, ui::ManagedObject next) {
  ui::ManagedObject previous = anchor;
  for (size_t i = 0; i < ROW_COUNT; ++i) {
    if (!ui::isAlive(state.rows[i].option)) continue;
    ui::ManagedObject following = next;
    for (size_t j = i + 1; j < ROW_COUNT; ++j) {
      if (ui::isAlive(state.rows[j].option)) {
        following = state.rows[j].option;
        break;
      }
    }
    setNavigation(state.rows[i].option, {NAVIGATION_EXPLICIT, false, previous, following, nullptr, nullptr});
    previous = state.rows[i].option;
  }
  return previous;
}

inline ui::ManagedObject firstRow(const MenuState& state) {
  for (const InjectedRow& row : state.rows) {
    if (ui::isAlive(row.option)) return row.option;
  }
  return nullptr;
}

/// SetupActive 之后：把我们的行接进导航
inline void spliceNavigation(const MenuState& state, ui::ManagedObject list) {
  if (!state.isInjected || list != state.list || !ui::isAlive(list)) return;
  ui::ManagedObject first = firstRow(state);
  ui::ManagedObject anchor = lastOriginalSelectable(state);
  if (first == nullptr || anchor == nullptr) return Logging.Log("[sshud] options: nothing to splice (anchor=%p)", anchor);
  Navigation anchorNav = navigationOf(anchor);
  if (anchorNav.selectOnDown == first) return;
  if (anchorNav.mode != NAVIGATION_EXPLICIT) return useAutomaticNavigation(state, anchorNav.mode);
  ui::ManagedObject next = anchorNav.selectOnDown;
  anchorNav.selectOnDown = first;
  setNavigation(anchor, anchorNav);
  ui::ManagedObject last = chainRows(state, anchor, next);
  if (ui::isAlive(next)) {
    Navigation nextNav = navigationOf(next);
    if (nextNav.selectOnUp == anchor) {
      nextNav.selectOnUp = last;
      setNavigation(next, nextNav);
    }
  }
  char anchorName[NAME_CAPACITY];
  char nextName[NAME_CAPACITY];
  Logging.Log("[sshud] options: navigation spliced %s -> rows -> %s",
              nameOf(componentTransform(anchor), anchorName, sizeof(anchorName)),
              ui::isAlive(next) ? nameOf(componentTransform(next), nextName, sizeof(nextName)) : "(none)");
}

}  // namespace sshud::options
