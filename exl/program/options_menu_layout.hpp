// 游戏选项页的排版：行太多时最后几行会被挤出屏幕。
//
// 实机量到（logLayoutOnce）：内容容器 Content 高 766、VerticalLayoutGroup 行距 105、上边距 50、
// 行本身高 0（controlH=1、没有 LayoutElement），没有 ScrollRect；「恢复默认」按钮在容器外。
// 原生 5 行 + 我们 7 行按 105 排到 y=-1205，超出容器。fitRowSpacing 按当前激活的行数压缩行距，
// 让最后一行落在容器内，上限是原来的行距。诊断日志每次运行只写一次（在布局已经算过之后调用）。

#pragma once

#include "options_menu_scan.hpp"

namespace sshud::options {

constexpr int32_t LAYOUT_PARENT_LEVELS = 6;
constexpr float LAYOUT_BOTTOM_MARGIN = 30.0f;  // 最后一行离容器底边留的空（文字以行位置为中心）
constexpr float SPACING_EPSILON = 0.5f;

inline ui::Rect rectOf(ui::ManagedObject rectTransform) { return call<ui::Rect>(RT_GETRECT, rectTransform); }
inline map::Vector2 anchoredOf(ui::ManagedObject rectTransform) {
  return call<map::Vector2>(RT_GETANCHOREDPOS, rectTransform);
}

inline void logLayoutGroup(ui::ManagedObject group) {
  ui::ManagedObject padding = call<ui::ManagedObject>(LG_GETPADDING, group);
  Logging.Log("[sshud] layout: group spacing=%.1f controlH=%d expandH=%d pad=%d/%d", call<float>(HVLG_GETSPACING, group),
              call<bool>(HVLG_GETCONTROLHEIGHT, group) ? 1 : 0, call<bool>(HVLG_GETFORCEEXPANDHEIGHT, group) ? 1 : 0,
              padding ? call<int32_t>(RECTOFFSET_GETTOP, padding) : -1,
              padding ? call<int32_t>(RECTOFFSET_GETBOTTOM, padding) : -1);
}

inline void logChild(int32_t index, ui::ManagedObject child) {
  char name[NAME_CAPACITY];
  const ui::Rect rect = rectOf(child);
  const map::Vector2 anchored = anchoredOf(child);
  ui::ManagedObject element = componentByName(gameObjectOf(child), "LayoutElement");
  Logging.Log("[sshud] layout:   #%d %s active=%d h=%.1f y=%.1f pref=%.1f", index, nameOf(child, name, sizeof(name)),
              isActiveSelf(gameObjectOf(child)) ? 1 : 0, rect.height, anchored.y,
              ui::isAlive(element) ? call<float>(LE_GETPREFERREDHEIGHT, element) : -1.0f);
}

inline void logParents(ui::ManagedObject content) {
  ui::ManagedObject at = content;
  for (int32_t level = 0; level < LAYOUT_PARENT_LEVELS && at != nullptr; ++level, at = parentOf(at)) {
    char name[NAME_CAPACITY];
    ui::ManagedObject go = gameObjectOf(at);
    const ui::Rect rect = rectOf(at);
    Logging.Log("[sshud] layout: up%d %s rect=(%.1f,%.1f %.1fx%.1f) y=%.1f scale=%.3f scroll=%d fitter=%d", level,
                nameOf(at, name, sizeof(name)), rect.x, rect.y, rect.width, rect.height, anchoredOf(at).y,
                call<ui::Vector3>(TR_GETLOCALSCALE, at).y, ui::isAlive(componentByName(go, "ScrollRect")) ? 1 : 0,
                ui::isAlive(componentByName(go, "ContentSizeFitter")) ? 1 : 0);
  }
}

inline ui::ManagedObject layoutGroupOf(ui::ManagedObject content) {
  ui::ManagedObject group = componentByName(gameObjectOf(content), "VerticalLayoutGroup");
  return ui::isAlive(group) ? group : nullptr;
}

inline int32_t activeChildCount(ui::ManagedObject content) {
  int32_t active = 0;
  const int32_t children = childCount(content);
  for (int32_t i = 0; i < children; ++i) active += isActiveSelf(gameObjectOf(childAt(content, i))) ? 1 : 0;
  return active;
}

/// 按激活行数压缩行距，让最后一行落在容器内；maxSpacing 是注入前的原始行距
inline void fitRowSpacing(ui::ManagedObject content, float maxSpacing) {
  ui::ManagedObject group = ui::isAlive(content) ? layoutGroupOf(content) : nullptr;
  const int32_t rows = group != nullptr ? activeChildCount(content) : 0;
  if (rows < 2 || maxSpacing <= 0.0f) return;
  ui::ManagedObject padding = call<ui::ManagedObject>(LG_GETPADDING, group);
  const float top = padding != nullptr ? static_cast<float>(call<int32_t>(RECTOFFSET_GETTOP, padding)) : 0.0f;
  const float height = rectOf(content).height;
  const float fit = (height - top - LAYOUT_BOTTOM_MARGIN) / static_cast<float>(rows - 1);
  const float spacing = fit < maxSpacing ? fit : maxSpacing;
  const float current = call<float>(HVLG_GETSPACING, group);
  if (current - spacing < SPACING_EPSILON && spacing - current < SPACING_EPSILON) return;
  call<void>(HVLG_SETSPACING, group, spacing);
  Logging.Log("[sshud] layout: %d rows in %.1f, spacing %.1f -> %.1f", rows, height, current, spacing);
}

inline float spacingOf(ui::ManagedObject content) {
  ui::ManagedObject group = layoutGroupOf(content);
  return group != nullptr ? call<float>(HVLG_GETSPACING, group) : 0.0f;
}

/// 布局算过之后调用；每次运行只写一次
inline void logLayoutOnce(ui::ManagedObject content) {
  static bool s_hasLogged = false;
  if (s_hasLogged || !ui::isAlive(content)) return;
  s_hasLogged = true;
  ui::ManagedObject group = componentByName(gameObjectOf(content), "VerticalLayoutGroup");
  if (ui::isAlive(group)) logLayoutGroup(group);
  logParents(content);
  const int32_t children = childCount(content);
  for (int32_t i = 0; i < children; ++i) logChild(i, childAt(content, i));
}

}  // namespace sshud::options
