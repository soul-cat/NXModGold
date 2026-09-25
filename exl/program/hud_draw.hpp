// 每帧在 PlayMakerGUI.OnGUI 里用 IMGUI 画 HUD：BOSS 条、普通敌人血条、伤害飘字（各自受开关控制），
// 以及按住 R3 时的开关面板。
//
// 外观对齐 PC 版 Silksong_HealthBar 的默认值（docs/tasks/hud-draw.md）。
// 只在 Repaint 事件里画（DrawTexture/Label 在其它事件里不出像素）。
// 用到的全局 IMGUI 状态（GUI.color / contentColor / skin.label 的字号与对齐）
// 进出时保存/还原，不影响游戏自己的 OnGUI。

#pragma once

#include "bar_textures.hpp"
#include "hud_frame.hpp"
#include "hud_model.hpp"
#include "hud_text_cache.hpp"
#include "menu_textures.hpp"
#include "toggles_model.hpp"
#include "unity_api.hpp"

#include <algorithm>

namespace sshud::hud {

// ---------------------------------------------------------------- 颜色（PC 默认）
constexpr ui::Color COLOR_BAR_FILL{0xBE / 255.0f, 0xB8 / 255.0f, 0xB8 / 255.0f, 1.0f};
constexpr ui::Color COLOR_ENEMY_BG{0.0f, 0.0f, 0.0f, 0x85 / 255.0f};
constexpr ui::Color COLOR_BOSS_BG{0.0f, 0.0f, 0.0f, 0x80 / 255.0f};
constexpr ui::Color COLOR_TEXT_DARK{0x0E / 255.0f, 0x04 / 255.0f, 0x04 / 255.0f, 1.0f};
constexpr ui::Color COLOR_TEXT_WHITE{1.0f, 1.0f, 1.0f, 1.0f};
constexpr ui::Color COLOR_BOSS_FRAME{1.0f, 1.0f, 1.0f, 0.95f};
constexpr ui::Color COLOR_DAMAGE{0xDC / 255.0f, 0x14 / 255.0f, 0x3C / 255.0f, 1.0f};
constexpr ui::Color COLOR_MENU_BG{0.0f, 0.0f, 0.0f, 0.7f};
constexpr ui::Color COLOR_MENU_ON{0.55f, 0.9f, 0.55f, 1.0f};
constexpr ui::Color COLOR_MENU_OFF{0.6f, 0.6f, 0.6f, 1.0f};

// ---------------------------------------------------------------- 字号（参考分辨率像素 / 世界单位）
constexpr float BOSS_NAME_FONT = 28.0f;
constexpr float BOSS_NUMBERS_FONT = 24.0f;
constexpr float BOSS_NAME_GAP = 28.0f;
constexpr float ENEMY_NUMBERS_FONT_UNITS = 0.32f;
constexpr float ENEMY_NUMBERS_GAP_UNITS = 0.3f;
constexpr float DAMAGE_FONT_UNITS = 0.55f;
// BOSS 贴图条：填充条在外框里的内缩量（相对外框高度；按 boss_frame.png 60px 高时 左右 10 / 上下 16 量得）
constexpr float BOSS_FILL_INSET_X_RATIO = 10.0f / 60.0f;
constexpr float BOSS_FILL_INSET_Y_RATIO = 16.0f / 60.0f;

// 开关面板（按住 R3）：屏幕上方居中，参考分辨率像素
constexpr float MENU_TOP = 48.0f;
constexpr float MENU_TEXT_HEIGHT = 34.0f;  // 文字贴图的绘制高度
constexpr float MENU_LINE_HEIGHT = 42.0f;
constexpr float MENU_PADDING = 16.0f;
constexpr float MENU_STATE_GAP = 40.0f;    // 功能名与 开/关 之间的最小间距

inline void drawBar(const Frame& frame, const Rect& bar, float ratio, ui::Color background) {
  ui::setGuiColor(background);
  ui::drawTexture(toUi(bar), frame.white);
  ui::setGuiColor(COLOR_BAR_FILL);
  ui::drawTexture(toUi(fillRect(bar, ratio, BAR_FILL_INSET)), frame.white);
  ui::setGuiColor(COLOR_TEXT_WHITE);
}

/// 外框 → 暗色空槽 → 按比例的填充（填充右端尖角跟着血量走）
inline void drawTexturedBossBar(const Rect& bar, float ratio, const BossBarTextures& textures) {
  drawSliced(textures.frame, bar, COLOR_BOSS_FRAME);
  const float insetX = bar.height * BOSS_FILL_INSET_X_RATIO;
  const float insetY = bar.height * BOSS_FILL_INSET_Y_RATIO;
  const Rect track{bar.x + insetX, bar.y + insetY, bar.width - 2.0f * insetX, bar.height - 2.0f * insetY};
  drawSliced(textures.fill, track, COLOR_BOSS_BG);
  drawSliced(textures.fill, {track.x, track.y, track.width * ratio, track.height}, COLOR_BAR_FILL);
  ui::setGuiColor(COLOR_TEXT_WHITE);
}

/// 一个要画的敌人：模型槽位下标（文字缓存按它索引）+ 本帧读到的血量
struct LiveEnemy {
  const Enemy& enemy;
  int slot;
  int32_t hp;
};

inline void drawBoss(const Frame& frame, const LiveEnemy& live, int index) {
  const Rect bar = bossBarRect(index, frame.width, frame.height);
  const float scale = uiScale(frame.width, frame.height);
  const float centerX = bar.x + bar.width * 0.5f;
  const float ratio = healthRatio(live.hp, live.enemy.maxHp);
  if (const BossBarTextures* textures = bossBarTextures(frame.now)) {
    drawTexturedBossBar(bar, ratio, *textures);
  } else {
    drawBar(frame, bar, ratio, COLOR_BOSS_BG);  // 贴图建不出来时退回纯色条
  }
  const ui::Color numbersColor = ratio < LOW_HEALTH_RATIO ? COLOR_TEXT_WHITE : COLOR_TEXT_DARK;
  drawText(frame, centerX, bar.y + bar.height * 0.5f, BOSS_NUMBERS_FONT * scale,
           healthTextFor(live.slot, live.hp, live.enemy.maxHp), numbersColor);
  drawText(frame, centerX, bar.y - BOSS_NAME_GAP * scale, BOSS_NAME_FONT * scale,
           bossNameFor(live.slot, live.enemy.manager), COLOR_TEXT_WHITE);
}

inline void drawEnemy(const Frame& frame, const LiveEnemy& live) {
  ui::Vector3 anchor = ui::worldPositionOf(live.enemy.manager);
  anchor.y += ENEMY_BAR_OFFSET_UNITS;
  float x = 0.0f;
  float y = 0.0f;
  if (!project(frame, anchor, x, y)) return;
  const float ppu = pixelsPerUnit(frame, anchor, x);
  if (ppu <= 0.0f) return;
  const float ratio = healthRatio(live.hp, live.enemy.maxHp);
  const Rect bar = enemyBarRect(x, y, ppu);
  drawBar(frame, bar, ratio, COLOR_ENEMY_BG);
  const ui::Color textColor = ratio < LOW_HEALTH_RATIO ? COLOR_TEXT_WHITE : COLOR_TEXT_DARK;
  drawText(frame, x, bar.y - ENEMY_NUMBERS_GAP_UNITS * ppu, ENEMY_NUMBERS_FONT_UNITS * ppu,
           healthTextFor(live.slot, live.hp, live.enemy.maxHp), textColor);
}

inline void drawPopup(const Frame& frame, const Popup& popup, int slot) {
  const float progress = popupProgress(frame.now, popup.startedAt);
  const ui::Vector3 world{popup.origin.x, popup.origin.y + POPUP_RISE_UNITS * progress, popup.origin.z};
  float x = 0.0f;
  float y = 0.0f;
  if (!project(frame, world, x, y)) return;
  const float ppu = pixelsPerUnit(frame, world, x);
  if (ppu <= 0.0f) return;
  ui::Color color = COLOR_DAMAGE;
  color.a = 1.0f - progress;
  drawText(frame, x, y, DAMAGE_FONT_UNITS * ppu, damageTextFor(slot, popup.damage), color);
}

/// 敌人已销毁 / 被停用 / 死亡 / 血量归零 → 返回 false，调用方应 forget
inline bool readLiveHp(const Enemy& enemy, int32_t& outHp) {
  if (!ui::isAlive(enemy.manager) || ui::readField<bool>(enemy.manager, HM_ISDEAD)) return false;
  outHp = ui::readField<int32_t>(enemy.manager, HM_HP);
  return outHp > 0 && ui::isActiveInHierarchy(enemy.manager);  // 托管调用放最后
}

/// 读活血量并清理失效条目（开关全关也要跑）；按开关决定画不画 BOSS 条 / 小怪血条
inline void drawEnemies(const Frame& frame, HudModel& model, const toggles::Settings& settings) {
  const bool isBossBarEnabled = settings.isEnabled(toggles::Feature::BossBar);
  const bool isEnemyBarEnabled = settings.isEnabled(toggles::Feature::EnemyBars) && frame.camera != nullptr;
  int bossIndex = 0;
  for (int i = 0; i < MAX_ENEMIES; ++i) {
    Enemy& enemy = model.enemies()[i];
    if (!enemy.active) continue;
    int32_t hp = 0;
    if (!readLiveHp(enemy, hp)) {
      enemy = Enemy{};
      continue;
    }
    if (isBoss(enemy.maxHp)) {
      if (isBossBarEnabled) drawBoss(frame, {enemy, i, hp}, bossIndex++);
    } else if (isEnemyBarEnabled) {
      drawEnemy(frame, {enemy, i, hp});
    }
  }
}

/// 按统一高度画一张文字贴图，返回绘制宽度
inline float drawTextImage(const TextImage& image, float x, float y, float height, ui::Color tint) {
  const float width = image.width * height / MENU_TEXT_SOURCE_HEIGHT;
  ui::setGuiColor(tint);
  ui::drawTexture({x, y, width, height}, image.texture);
  return width;
}

/// 面板内容宽度（参考分辨率像素）：标题与 "功能名 + 间距 + 开/关" 取最宽
inline float menuContentWidth(const MenuTextures& textures) {
  const float toReference = MENU_TEXT_HEIGHT / MENU_TEXT_SOURCE_HEIGHT;
  float widest = textures.title.width * toReference;
  const float stateWidth = std::max(textures.on.width, textures.off.width) * toReference;
  for (const TextImage& row : textures.rows) {
    widest = std::max(widest, row.width * toReference + MENU_STATE_GAP + stateWidth);
  }
  return widest;
}

/// 一行功能：功能名靠左（关闭时变灰），开/关 靠右
inline void drawMenuRow(const MenuTextures& textures, size_t index, bool isEnabled, const Rect& line) {
  const TextImage& state = isEnabled ? textures.on : textures.off;
  const float stateWidth = state.width * line.height / MENU_TEXT_SOURCE_HEIGHT;
  drawTextImage(textures.rows[index], line.x, line.y, line.height, isEnabled ? COLOR_TEXT_WHITE : COLOR_MENU_OFF);
  drawTextImage(state, line.x + line.width - stateWidth, line.y, line.height, isEnabled ? COLOR_MENU_ON : COLOR_MENU_OFF);
}

/// 开关面板：半透明底框 + 标题 + 每个功能一行（中文文字是预渲染贴图）
inline void drawMenu(const Frame& frame, const toggles::Settings& settings, bool isMenuOpen) {
  if (!isMenuOpen) return;
  const MenuTextures* textures = menuTextures(frame.now);
  if (textures == nullptr) return;
  const float scale = uiScale(frame.width, frame.height);
  const float padding = MENU_PADDING * scale;
  const float lineHeight = MENU_LINE_HEIGHT * scale;
  const float contentWidth = menuContentWidth(*textures) * scale;
  const float boxX = (frame.width - contentWidth) * 0.5f - padding;
  const float lineCount = static_cast<float>(toggles::PANEL_FEATURE_COUNT + 1);  // 标题 + 每个功能一行
  ui::setGuiColor(COLOR_MENU_BG);
  ui::drawTexture({boxX, MENU_TOP * scale, contentWidth + 2.0f * padding, lineHeight * lineCount + 2.0f * padding},
                  frame.white);
  Rect line{boxX + padding, MENU_TOP * scale + padding + (lineHeight - MENU_TEXT_HEIGHT * scale) * 0.5f, contentWidth,
            MENU_TEXT_HEIGHT * scale};
  drawTextImage(textures->title, line.x, line.y, line.height, COLOR_TEXT_WHITE);
  for (size_t i = 0; i < toggles::PANEL_FEATURE_COUNT; ++i) {
    line.y += lineHeight;
    drawMenuRow(*textures, i, settings.enabled[i], line);
  }
  ui::setGuiColor(COLOR_TEXT_WHITE);
}

/// OnGUI 入口：每个 IMGUI 事件都会调用，只在 Repaint 时画
inline void drawHud(HudModel& model, const toggles::Settings& settings, bool isMenuOpen) {
  if (ui::currentEventType() != ui::EVENT_TYPE_REPAINT) return;
  const float now = ui::unscaledTime();
  model.expire(now);
  releaseInactiveTexts(model);
  if (model.isEmpty() && !isMenuOpen) return;  // 没东西可画：省掉下面保存/还原状态与取屏幕/相机的约 20 次托管调用
  ImguiStateGuard guard;
  const Frame frame = beginFrame(guard.style(), now);
  drawEnemies(frame, model, settings);
  if (frame.camera != nullptr && settings.isEnabled(toggles::Feature::DamageNumbers)) {
    for (int i = 0; i < MAX_POPUPS; ++i) {
      if (model.popups()[i].active) drawPopup(frame, model.popups()[i], i);
    }
  }
  drawMenu(frame, settings, isMenuOpen);  // 最后画，压在最上层
}

}  // namespace sshud::hud
