// BOSS 挑战菜单的 IMGUI 绘制：屏幕中央半透明框，标题（第几幕）+ 两个固定项 + 这一幕的 BOSS + 操作提示。
// 项多于 MAX_VISIBLE_ITEMS 时只画光标附近的一段（firstVisibleItem）。
// 文字用 HUD 字体（hud_font.hpp，游戏自己的 Font），中文 BOSS 名是游戏官方译名。

#pragma once

#include "boss_rush.hpp"
#include "hud_frame.hpp"

namespace sshud::hud {

constexpr float BOSS_MENU_WIDTH = 820.0f;  // 参考分辨率像素
constexpr float BOSS_MENU_LINE_HEIGHT = 46.0f;
constexpr float BOSS_MENU_PADDING = 18.0f;
constexpr float BOSS_MENU_TITLE_FONT = 30.0f;
constexpr float BOSS_MENU_ITEM_FONT = 28.0f;
constexpr float BOSS_MENU_HINT_FONT = 22.0f;
constexpr int BOSS_MENU_EXTRA_LINES = 2;  // 标题 + 提示
constexpr ui::Color COLOR_BOSS_MENU_BG{0.0f, 0.0f, 0.0f, 0.78f};
constexpr ui::Color COLOR_BOSS_MENU_SELECTED{1.0f, 0.86f, 0.45f, 1.0f};
constexpr ui::Color COLOR_BOSS_MENU_ITEM{0.9f, 0.9f, 0.9f, 1.0f};
constexpr ui::Color COLOR_BOSS_MENU_HINT{0.6f, 0.6f, 0.6f, 1.0f};
constexpr ui::Color COLOR_BOSS_MENU_WHITE{1.0f, 1.0f, 1.0f, 1.0f};

inline void drawBossMenu(const Frame& frame) {
  const bossrush::BossMenu& menu = bossrush::g_menu;
  if (!menu.isOpen() || !bossrush::g_texts.isReady) return;
  const float scale = uiScale(frame.width, frame.height);
  const float width = BOSS_MENU_WIDTH * scale;
  const float line = BOSS_MENU_LINE_HEIGHT * scale;
  const float padding = BOSS_MENU_PADDING * scale;
  const int visible = menu.itemCount() < bossrush::MAX_VISIBLE_ITEMS ? menu.itemCount() : bossrush::MAX_VISIBLE_ITEMS;
  const int first = bossrush::firstVisibleItem(menu.cursor(), menu.itemCount(), bossrush::MAX_VISIBLE_ITEMS);
  const float height = line * static_cast<float>(visible + BOSS_MENU_EXTRA_LINES) + padding * 2.0f;
  const float left = (frame.width - width) * 0.5f;
  const float top = (frame.height - height) * 0.5f;
  const float centerX = frame.width * 0.5f;
  ui::setGuiColor(COLOR_BOSS_MENU_BG);
  ui::drawTexture({left, top, width, height}, frame.white);
  ui::setGuiColor(COLOR_BOSS_MENU_WHITE);
  float y = top + padding + line * 0.5f;
  drawTextInWidth(frame, centerX, y, width, BOSS_MENU_TITLE_FONT * scale, bossrush::g_texts.actTitles[menu.act() - 1],
                  COLOR_BOSS_MENU_WHITE);
  for (int item = first; item < first + visible; ++item) {
    y += line;
    const bool isSelected = item == menu.cursor();
    drawTextInWidth(frame, centerX, y, width, BOSS_MENU_ITEM_FONT * scale, bossrush::itemText(menu.act(), item),
                    isSelected ? COLOR_BOSS_MENU_SELECTED : COLOR_BOSS_MENU_ITEM);
  }
  drawTextInWidth(frame, centerX, y + line, width, BOSS_MENU_HINT_FONT * scale, bossrush::g_texts.hint,
                  COLOR_BOSS_MENU_HINT);
}

}  // namespace sshud::hud
