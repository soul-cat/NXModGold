// 我们插进游戏选项页的四个开关行：克隆、摆位、改标签、同步值。
//
// 克隆时页面还没激活（UIGoToGameOptionsMenu 先等当前菜单淡出才 ShowMenu），克隆体的 Awake / Start 都没跑，
// 所以立即销毁它的 MenuSetting（否则 Start 会把游戏设置的值刷进来、改动时去改游戏设置）
// 和标签上的 AutoLocalizeTextUI（否则 OnEnable 会把标签换回模板行的本地化文字）。
// 选项文字沿用模板的本地化键，开 / 关跟随游戏语言与字体。
//
// 摆位：内容容器有 VerticalLayoutGroup 时只排兄弟序号，布局自己算位置；
// 没有就按锚点行与上一行的间距往下排，并把锚点之后的兄弟节点整体下移四行。

#pragma once

#include "options_menu_scan.hpp"
#include "toggles.hpp"

namespace sshud::options {

struct InjectedRow {
  ui::ManagedObject row = nullptr;     // 行的 GameObject
  ui::ManagedObject option = nullptr;  // 行里的 MenuOptionHorizontal
  ui::PinHandle rowPin = 0;
  ui::PinHandle optionPin = 0;
};

struct Placement {
  ui::ManagedObject content = nullptr;
  ui::ManagedObject anchorRow = nullptr;
  int32_t anchorSibling = 0;
  ui::Vector3 anchorPos{};
  ui::Vector3 step{};
  bool hasLayout = false;
  bool hasStep = false;
};

inline ui::Vector3 offset(ui::Vector3 p, ui::Vector3 step, float times) {
  return {p.x + step.x * times, p.y + step.y * times, p.z + step.z * times};
}

inline Placement planPlacement(const ScreenScan& scan, ui::ManagedObject templateOption) {
  Placement plan;
  plan.content = parentOf(rowOf(templateOption));
  plan.anchorRow = lastActiveRow(scan, plan.content, -1);
  if (plan.anchorRow == nullptr) return plan;
  plan.anchorSibling = siblingIndexOf(plan.anchorRow);
  plan.anchorPos = localPositionOf(plan.anchorRow);
  plan.hasLayout = ui::isAlive(componentByName(gameObjectOf(plan.content), "VerticalLayoutGroup"));
  ui::ManagedObject previous = lastActiveRow(scan, plan.content, plan.anchorSibling);
  if (previous != nullptr) {
    const ui::Vector3 p = localPositionOf(previous);
    plan.step = {plan.anchorPos.x - p.x, plan.anchorPos.y - p.y, 0.0f};
    plan.hasStep = plan.step.x != 0.0f || plan.step.y != 0.0f;
  }
  char content[NAME_CAPACITY];
  char anchor[NAME_CAPACITY];
  Logging.Log("[sshud] options: content=%s anchor=%s sib=%d pos=(%.1f,%.1f) step=(%.1f,%.1f) layout=%d",
              nameOf(plan.content, content, sizeof(content)), nameOf(plan.anchorRow, anchor, sizeof(anchor)),
              plan.anchorSibling, plan.anchorPos.x, plan.anchorPos.y, plan.step.x, plan.step.y, plan.hasLayout ? 1 : 0);
  return plan;
}

/// 无布局组时：锚点之后（沿行间距方向）的兄弟节点下移四行，给新行腾位置
inline void shiftFollowers(const Placement& plan) {
  if (plan.hasLayout || !plan.hasStep) return;
  const int32_t children = childCount(plan.content);
  for (int32_t i = 0; i < children; ++i) {
    ui::ManagedObject child = childAt(plan.content, i);
    const ui::Vector3 p = localPositionOf(child);
    const float along = (p.x - plan.anchorPos.x) * plan.step.x + (p.y - plan.anchorPos.y) * plan.step.y;
    if (along <= 0.0f) continue;
    setLocalPosition(child, offset(p, plan.step, static_cast<float>(toggles::FEATURE_COUNT)));
    char name[NAME_CAPACITY];
    Logging.Log("[sshud] options: shifted %s from (%.1f,%.1f)", nameOf(child, name, sizeof(name)), p.x, p.y);
  }
}

/// 克隆体的 MenuSetting 在子树里才销毁（引用指向子树外时是原行的，不能动）；断开与游戏设置的一切联系
inline void detachFromGameSettings(ui::ManagedObject option, ui::ManagedObject rowTr) {
  ui::ManagedObject setting = ui::readField<ui::ManagedObject>(option, MOH_MENUSETTING);
  if (ui::isAlive(setting) && isUnder(componentTransform(setting), rowTr)) destroyNow(setting);
  ui::writeField<ui::ManagedObject>(option, MOH_MENUSETTING, nullptr);
  ui::writeField<ui::ManagedObject>(option, MOH_REFRESHLIST, nullptr);
  ui::writeField<int32_t>(option, MOH_APPLYSETTINGON, APPLY_ON_SCROLL);
}

constexpr size_t MAX_LOCALIZERS = 4;

/// 去掉行里除选项文字外的自动本地化，第一个文本写成我们的标签，其余清空
inline void relabel(ui::ManagedObject option, ui::ManagedObject rowTr, const char16_t* label) {
  ui::ManagedObject localizers[MAX_LOCALIZERS] = {};
  size_t count = 0;
  collectInTree(rowTr, "AutoLocalizeTextUI", 0, localizers, MAX_LOCALIZERS, count);
  const ui::ManagedObject optionText = ui::readField<ui::ManagedObject>(option, MOH_OPTIONTEXT);
  bool isLabelled = false;
  for (size_t i = 0; i < count; ++i) {
    const ui::ManagedObject text = ui::readField<ui::ManagedObject>(localizers[i], ALT_TEXTFIELD);
    if (text == optionText) continue;
    destroyNow(localizers[i]);
    if (!ui::isAlive(text)) continue;
    setText(text, isLabelled ? u"" : label);
    if (!isLabelled) Logging.Log("[sshud] options: label set, missing glyphs=%d", missingGlyphs(text, label));
    isLabelled = true;
  }
  if (!isLabelled) Logging.Log("[sshud] options: no label text found (%zu localizers)", count);
}

/// 在内容容器里克隆第 feature 行并改造好；失败返回空行（已建的半成品会销毁）
inline InjectedRow createRow(ui::ManagedObject templateOption, const Placement& plan, size_t feature,
                             int32_t language) {
  InjectedRow made;
  ui::ManagedObject row = call<ui::ManagedObject>(UOBJ_INSTANTIATE, gameObjectOf(rowOf(templateOption)), plan.content, false);
  if (row == nullptr) return made;
  char name[NAME_CAPACITY];
  std::snprintf(name, sizeof(name), "sshud_opt_%s", toggles::FEATURES[feature].key);
  call<void>(UOBJ_SETNAME, row, map::managedString(name));
  ui::ManagedObject rowTr = transformOf(row);
  const int32_t order = static_cast<int32_t>(feature) + 1;
  setSiblingIndex(rowTr, plan.anchorSibling + order);
  if (!plan.hasLayout && plan.hasStep) setLocalPosition(rowTr, offset(plan.anchorPos, plan.step, static_cast<float>(order)));
  ui::ManagedObject option = findInTree(rowTr, "MenuOptionHorizontal");
  if (option == nullptr) {
    Logging.Log("[sshud] options: clone %s has no MenuOptionHorizontal", name);
    destroyNow(row);
    return made;
  }
  detachFromGameSettings(option, rowTr);
  relabel(option, rowTr, labelFor(feature, language));
  made = {row, option, ui::pinObject(row), ui::pinObject(option)};
  return made;
}

/// 把开关当前值刷到各行
inline void syncRows(const InjectedRow* rows, int32_t onIndex) {
  for (size_t i = 0; i < toggles::FEATURE_COUNT; ++i) {
    if (!ui::isAlive(rows[i].option)) continue;
    const bool isOn = toggles::g_settings.enabled[i];
    setOptionTo(rows[i].option, isOn ? onIndex : 1 - onIndex);
  }
}

}  // namespace sshud::options
