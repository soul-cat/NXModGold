// 把四个功能开关做进游戏原生的「选项 → 游戏」页（与 R3 面板共用 toggles::g_settings 与 config.ini）。
//
// 三个 hook：
//   1. UIManager.UIGoToGameOptionsMenu（前）—— 页面激活之前：第一次（或页面被重建后）克隆插入四行，
//      之后每次打开把开关当前值刷到行上（R3 面板可能改过）。
//   2. MenuButtonList.SetupActive（后）—— 页面激活时游戏按 activeSelectables 重排上下导航，
//      我们的行不在它的 entries 里，所以在它之后把四行接到最后一个原生选项行与它原来的下一项之间。
//   3. MenuOptionHorizontal.UpdateSetting（后）—— 左右切换后调用；是我们的行就改开关并写回 SD。
//      克隆行的 menuSetting 已清空，原函数对它什么都不做（反汇编：只在 menuSetting 存活时 ChangeSetting）。
// 都在 Unity 主线程。跨帧持有的行与列表用 GCHandle 钉住。

#pragma once

#include "options_menu_rows.hpp"
#include "sd_logger.hpp"

#include <algorithm>

namespace sshud::options {

constexpr int32_t NAVIGATION_AUTOMATIC = 3;  // UnityEngine.UI.Navigation.Mode.Automatic

struct MenuState {
  ui::ManagedObject screen = nullptr;  // 注入时的游戏选项页（只比较，不解引用）
  ui::ManagedObject list = nullptr;
  ui::PinHandle listPin = 0;
  ui::ManagedObject originals[MAX_SCANNED_OPTIONS] = {};  // 原生选项行（只比较）
  size_t originalCount = 0;
  InjectedRow rows[toggles::FEATURE_COUNT];
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

/// 模板两个选项里哪个是"开"
inline int32_t resolveOnIndex(ui::ManagedObject templateOption) {
  char key0[LOG_KEY_CAPACITY];
  char key1[LOG_KEY_CAPACITY];
  const OnIndex on = onIndexFromKeys(optionKey(templateOption, 0, key0, sizeof(key0)),
                                     optionKey(templateOption, 1, key1, sizeof(key1)));
  Logging.Log("[sshud] options: template keys %s,%s -> on=%d%s", key0, key1, on.index, on.isCertain ? "" : " (guess)");
  return on.index;
}

/// 在页面里建四行；只要找到模板就算注入过（建失败的行不重试，免得反复叠加）
inline void inject(MenuState& state, ui::ManagedObject screen) {
  const ScreenScan scan = scanScreen(componentTransform(screen));
  ui::ManagedObject templateOption = chooseTemplate(scan);
  if (templateOption == nullptr) return Logging.Log("[sshud] options: no on/off row to clone, giving up");
  const Placement plan = planPlacement(scan, templateOption);
  if (plan.anchorRow == nullptr) return Logging.Log("[sshud] options: no active row in content, giving up");
  state.screen = screen;
  state.list = scan.list;
  state.listPin = ui::pinObject(scan.list);
  std::copy(scan.options, scan.options + scan.optionCount, state.originals);
  state.originalCount = scan.optionCount;
  state.onIndex = resolveOnIndex(templateOption);
  state.isInjected = true;
  shiftFollowers(plan);
  const int32_t language = currentLanguage();
  size_t made = 0;
  for (size_t i = 0; i < toggles::FEATURE_COUNT; ++i) {
    state.rows[i] = createRow(templateOption, plan, i, language);
    if (state.rows[i].row != nullptr) ++made;
  }
  Logging.Log("[sshud] options: injected %zu/%zu rows (language=%d)", made, toggles::FEATURE_COUNT, language);
}

/// 打开游戏选项页之前：确保四行在，刷新它们的值
inline void prepareScreen(ui::ManagedObject uiManager) {
  sdlog::start();
  toggles::loadOnce();
  ui::ManagedObject screen = ui::readField<ui::ManagedObject>(uiManager, UIM_GAMEOPTIONSSCREEN);
  if (!ui::isAlive(screen)) return Logging.Log("[sshud] options: gameOptionsMenuScreen missing");
  if (!g_menu.isIntact(screen)) {
    if (g_menu.isInjected) Logging.Log("[sshud] options: screen rebuilt, re-injecting");
    g_menu.discard();
    inject(g_menu, screen);
  }
  syncRows(g_menu.rows, g_menu.onIndex);
}

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
  for (size_t i = 0; i < toggles::FEATURE_COUNT; ++i) {
    if (!ui::isAlive(state.rows[i].option)) continue;
    ui::ManagedObject following = next;
    for (size_t j = i + 1; j < toggles::FEATURE_COUNT; ++j) {
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

/// UpdateSetting 之后：是我们的行就改开关并写回 SD
inline void applyRowChange(const MenuState& state, ui::ManagedObject option) {
  for (size_t i = 0; i < toggles::FEATURE_COUNT; ++i) {
    if (state.rows[i].option != option) continue;
    const bool isOn = ui::readField<int32_t>(option, MOH_SELECTEDINDEX) == state.onIndex;
    if (toggles::g_settings.enabled[i] == isOn) return;
    toggles::g_settings.enabled[i] = isOn;
    toggles::saveSettings(toggles::g_settings);
    Logging.Log("[sshud] options: %s=%d", toggles::FEATURES[i].key, isOn ? 1 : 0);
    return;
  }
}

}  // namespace sshud::options

HOOK_DEFINE_TRAMPOLINE(GoToGameOptionsHook) {
  static void Callback(void* self, const void* method) {
    if (sshud::unity::isAlive(self)) sshud::options::prepareScreen(self);
    Orig(self, method);
  }
};

HOOK_DEFINE_TRAMPOLINE(MenuListSetupActiveHook) {
  static void Callback(void* self, const void* method) {
    Orig(self, method);
    sshud::options::spliceNavigation(sshud::options::g_menu, self);
  }
};

HOOK_DEFINE_TRAMPOLINE(OptionUpdateSettingHook) {
  static void Callback(void* self, const void* method) {
    Orig(self, method);
    sshud::options::applyRowChange(sshud::options::g_menu, self);
  }
};

namespace sshud::options {

inline void installHooks() {
  GoToGameOptionsHook::InstallAtOffset(UIM_UIGOTOGAMEOPTIONS);
  MenuListSetupActiveHook::InstallAtOffset(MBL_SETUPACTIVE);
  OptionUpdateSettingHook::InstallAtOffset(MOH_UPDATESETTING);
  Logging.Log("[sshud] options hooks installed: UIGoToGameOptionsMenu, MenuButtonList.SetupActive, UpdateSetting");
}

}  // namespace sshud::options
