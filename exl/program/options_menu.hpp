// 把功能开关与新存档起始纹章做进游戏原生的「选项 → 游戏」页（与 R3 面板共用 toggles::g_settings 与 config.ini）。
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

#include "options_menu_layout.hpp"
#include "options_menu_nav.hpp"
#include "sd_logger.hpp"
#include "start_crest.hpp"

#include <algorithm>

namespace sshud::options {

/// 模板两个选项里哪个是"开"
inline int32_t resolveOnIndex(ui::ManagedObject templateOption) {
  char key0[LOG_KEY_CAPACITY];
  char key1[LOG_KEY_CAPACITY];
  const OnIndex on = onIndexFromKeys(optionKey(templateOption, 0, key0, sizeof(key0)),
                                     optionKey(templateOption, 1, key1, sizeof(key1)));
  Logging.Log("[sshud] options: template keys %s,%s -> on=%d%s", key0, key1, on.index, on.isCertain ? "" : " (guess)");
  return on.index;
}

/// 克隆来的开 / 关行换成 9 项纹章列表；换不成就把这行拆掉（留着 2 项列表的话 SetOptionTo 会越界）
inline void finishCrestRow(InjectedRow& row) {
  if (row.option == nullptr || crest::configureOptionRow(row.option)) return;
  if (ui::isAlive(row.row)) destroyNow(row.row);
  ui::unpinObject(row.rowPin);
  ui::unpinObject(row.optionPin);
  row = InjectedRow{};
}

/// 在页面里建各行；只要找到模板就算注入过（建失败的行不重试，免得反复叠加）
inline void inject(MenuState& state, ui::ManagedObject screen) {
  const ScreenScan scan = scanScreen(componentTransform(screen));
  ui::ManagedObject templateOption = chooseTemplate(scan);
  if (templateOption == nullptr) return Logging.Log("[sshud] options: no on/off row to clone, giving up");
  const Placement plan = planPlacement(scan, templateOption);
  if (plan.anchorRow == nullptr) return Logging.Log("[sshud] options: no active row in content, giving up");
  state.screen = screen;
  state.list = scan.list;
  state.content = plan.content;
  state.maxSpacing = spacingOf(plan.content);
  state.contentPos = localPositionOf(plan.content);
  state.listPin = ui::pinObject(scan.list);
  std::copy(scan.options, scan.options + scan.optionCount, state.originals);
  state.originalCount = scan.optionCount;
  state.onIndex = resolveOnIndex(templateOption);
  state.isInjected = true;
  shiftFollowers(plan);
  const int32_t language = currentLanguage();
  size_t made = 0;
  for (size_t i = 0; i < ROW_COUNT; ++i) {
    state.rows[i] = createRow(templateOption, plan, i, language);
    if (i == CREST_ROW) finishCrestRow(state.rows[i]);
    if (state.rows[i].row != nullptr) ++made;
  }
  Logging.Log("[sshud] options: injected %zu/%zu rows (language=%d)", made, ROW_COUNT, language);
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

inline void applyFeatureChange(size_t feature, bool isOn) {
  if (toggles::g_settings.enabled[feature] == isOn) return;
  toggles::g_settings.enabled[feature] = isOn;
  toggles::saveSettings(toggles::g_settings);
  Logging.Log("[sshud] options: %s=%d", toggles::FEATURES[feature].key, isOn ? 1 : 0);
  sdlog::flushNow();
}

inline void applyCrestChange(int32_t index) {
  if (index < 0 || index >= toggles::START_CREST_COUNT || toggles::g_settings.startCrest == index) return;
  toggles::g_settings.startCrest = index;
  toggles::saveSettings(toggles::g_settings);
  Logging.Log("[sshud] options: %s=%d (%s)", toggles::START_CREST_KEY, index, crest::crestIdOf(index));
  sdlog::flushNow();
}

/// UpdateSetting 之后：是我们的行就改设置并写回 SD
inline void applyRowChange(const MenuState& state, ui::ManagedObject option) {
  for (size_t i = 0; i < ROW_COUNT; ++i) {
    if (state.rows[i].option != option) continue;
    const int32_t selected = ui::readField<int32_t>(option, MOH_SELECTEDINDEX);
    if (i == CREST_ROW) return applyCrestChange(selected);
    return applyFeatureChange(i, selected == state.onIndex);
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
    namespace options = sshud::options;
    options::spliceNavigation(options::g_menu, self);
    const options::MenuState& menu = options::g_menu;
    if (self == menu.list) options::fitRows(menu.content, menu.maxSpacing, menu.contentPos);
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
