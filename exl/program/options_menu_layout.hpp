// 游戏选项页的排版：我们的行加进去后超出了内容容器，最后几行被挤出屏幕。
//
// 实机量到：内容容器 Content 高 766、pivot 居中；VerticalLayoutGroup 行距 105、上边距 50；
// 行本身高 0（controlH=1、没有 LayoutElement）；没有 ScrollRect；「恢复默认」按钮在容器外。
// 原生 5 行 + 我们 7 行按 105 排到 y=-1205。按 fitLayout（options_menu_model.hpp）先压行距、
// 再等比缩小整个容器，缩放时保持容器顶边不动。每次页面激活（SetupActive 之后）按当前激活的行数重算。
// 只对 Content 本身调 RectTransform 接口：往上的 _UIManager 不是 RectTransform，对它调会读到垃圾。

#pragma once

#include "options_menu_scan.hpp"

#include <cmath>

namespace sshud::options {

constexpr float LAYOUT_EPSILON = 0.001f;

inline ui::Rect rectOf(ui::ManagedObject rectTransform) { return call<ui::Rect>(RT_GETRECT, rectTransform); }

inline ui::ManagedObject layoutGroupOf(ui::ManagedObject content) {
  ui::ManagedObject group = componentByName(gameObjectOf(content), "VerticalLayoutGroup");
  return ui::isAlive(group) ? group : nullptr;
}

inline float spacingOf(ui::ManagedObject content) {
  ui::ManagedObject group = layoutGroupOf(content);
  return group != nullptr ? call<float>(HVLG_GETSPACING, group) : 0.0f;
}

inline int32_t activeChildCount(ui::ManagedObject content) {
  int32_t active = 0;
  const int32_t children = childCount(content);
  for (int32_t i = 0; i < children; ++i) active += isActiveSelf(gameObjectOf(childAt(content, i))) ? 1 : 0;
  return active;
}

inline float topPaddingOf(ui::ManagedObject group) {
  ui::ManagedObject padding = call<ui::ManagedObject>(LG_GETPADDING, group);
  return padding != nullptr ? static_cast<float>(call<int32_t>(RECTOFFSET_GETTOP, padding)) : 0.0f;
}

/// 等比缩放容器并保持顶边不动（pivot 不在顶边时补偿位置）；originalPos 是注入前的 localPosition
inline void scaleKeepingTop(ui::ManagedObject content, ui::Vector3 originalPos, const ui::Rect& rect, float scale) {
  const float top = rect.y + rect.height;
  call<void>(TR_SETLOCALSCALE, content, ui::Vector3{scale, scale, 1.0f});
  setLocalPosition(content, {originalPos.x, originalPos.y + (1.0f - scale) * top, originalPos.z});
}

/// 按当前激活行数重排：行距 + 整体缩放；maxSpacing / originalPos 是注入前的值
inline void fitRows(ui::ManagedObject content, float maxSpacing, ui::Vector3 originalPos) {
  ui::ManagedObject group = ui::isAlive(content) ? layoutGroupOf(content) : nullptr;
  if (group == nullptr) return;
  const int32_t rows = activeChildCount(content);
  const ui::Rect rect = rectOf(content);
  const LayoutFit fit = fitLayout(rows, rect.height, topPaddingOf(group), maxSpacing);
  const float spacing = call<float>(HVLG_GETSPACING, group);
  const float scale = call<ui::Vector3>(TR_GETLOCALSCALE, content).y;
  if (std::fabs(spacing - fit.spacing) < LAYOUT_EPSILON && std::fabs(scale - fit.scale) < LAYOUT_EPSILON) return;
  call<void>(HVLG_SETSPACING, group, fit.spacing);
  scaleKeepingTop(content, originalPos, rect, fit.scale);
  Logging.Log("[sshud] layout: %d rows in %.1f -> spacing %.1f scale %.3f", rows, rect.height, fit.spacing, fit.scale);
}

}  // namespace sshud::options
