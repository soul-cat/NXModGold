// 每帧在 PlayMakerGUI.OnGUI 里用 IMGUI 画 HUD：BOSS 条、普通敌人血条、伤害飘字。
//
// 外观对齐 PC 版 Silksong_HealthBar 的默认值（docs/tasks/hud-draw.md）。
// 只在 Repaint 事件里画（DrawTexture/Label 在其它事件里不出像素）。
// 用到的全局 IMGUI 状态（GUI.color / contentColor / skin.label 的字号与对齐）
// 进出时保存/还原，不影响游戏自己的 OnGUI。

#pragma once

#include "hud_model.hpp"
#include "toggles_model.hpp"
#include "unity_api.hpp"

#include <algorithm>
#include <cmath>

namespace sshud::hud {

namespace ui = sshud::unity;

// ---------------------------------------------------------------- 颜色（PC 默认）
constexpr ui::Color COLOR_BAR_FILL{0xBE / 255.0f, 0xB8 / 255.0f, 0xB8 / 255.0f, 1.0f};
constexpr ui::Color COLOR_ENEMY_BG{0.0f, 0.0f, 0.0f, 0x85 / 255.0f};
constexpr ui::Color COLOR_BOSS_BG{0.0f, 0.0f, 0.0f, 0x80 / 255.0f};
constexpr ui::Color COLOR_TEXT_DARK{0x0E / 255.0f, 0x04 / 255.0f, 0x04 / 255.0f, 1.0f};
constexpr ui::Color COLOR_TEXT_WHITE{1.0f, 1.0f, 1.0f, 1.0f};
constexpr ui::Color COLOR_DAMAGE{0xDC / 255.0f, 0x14 / 255.0f, 0x3C / 255.0f, 1.0f};

// ---------------------------------------------------------------- 字号（参考分辨率像素 / 世界单位）
constexpr float BOSS_NAME_FONT = 28.0f;
constexpr float BOSS_NUMBERS_FONT = 24.0f;
constexpr float BOSS_NAME_GAP = 28.0f;
constexpr float ENEMY_NUMBERS_FONT_UNITS = 0.32f;
constexpr float ENEMY_NUMBERS_GAP_UNITS = 0.3f;
constexpr float DAMAGE_FONT_UNITS = 0.55f;
constexpr float TEXT_BOX_WIDTH_FACTOR = 6.0f;  // 文字框宽度 = 字号 × 此值，足够放下 "9999/9999"

constexpr char16_t HEALTH_SEPARATOR = u'/';

// 调试标记：左上角画一个小方块，证明 IMGUI 绘制已经上屏（不依赖打怪）
constexpr bool SHOW_DEBUG_MARKER = true;
constexpr float DEBUG_MARKER_SIZE = 12.0f;
constexpr float DEBUG_MARKER_MARGIN = 8.0f;

// 开关提示：屏幕上方居中，参考分辨率像素
constexpr float TOAST_FONT = 26.0f;
constexpr float TOAST_TOP = 48.0f;

/// 保存并在析构时还原本模块会改动的 IMGUI 全局状态
class ImguiStateGuard {
 public:
  ImguiStateGuard()
      : style_(ui::labelStyle()),
        color_(ui::guiColor()),
        contentColor_(ui::guiContentColor()),
        fontSize_(ui::styleFontSize(style_)),
        alignment_(ui::styleAlignment(style_)) {
    ui::setStyleAlignment(style_, ui::TEXT_ANCHOR_MIDDLE_CENTER);
  }
  ~ImguiStateGuard() {
    ui::setGuiColor(color_);
    ui::setGuiContentColor(contentColor_);
    ui::setStyleFontSize(style_, fontSize_);
    ui::setStyleAlignment(style_, alignment_);
  }
  ImguiStateGuard(const ImguiStateGuard&) = delete;
  ImguiStateGuard& operator=(const ImguiStateGuard&) = delete;

  ui::ManagedObject style() const { return style_; }

 private:
  ui::ManagedObject style_;
  ui::Color color_;
  ui::Color contentColor_;
  int32_t fontSize_;
  int32_t alignment_;
};

/// 一帧的绘制上下文
struct Frame {
  float now;
  float width;
  float height;
  ui::ManagedObject camera;  // 可能为 null（场景切换中）
  float cameraScaleX;        // 相机像素 → 屏幕像素
  float cameraScaleY;
  ui::ManagedObject white;
  ui::ManagedObject style;
};

inline ui::Rect toUi(const Rect& r) { return {r.x, r.y, r.width, r.height}; }

inline void drawBar(const Frame& frame, const Rect& bar, float ratio, ui::Color background) {
  ui::setGuiColor(background);
  ui::drawTexture(toUi(bar), frame.white);
  ui::setGuiColor(COLOR_BAR_FILL);
  ui::drawTexture(toUi(fillRect(bar, ratio, BAR_FILL_INSET)), frame.white);
  ui::setGuiColor(COLOR_TEXT_WHITE);
}

/// 以 (centerX, centerY) 为中心画一行字
inline void drawText(const Frame& frame, float centerX, float centerY, float fontSize,
                     ui::ManagedString text, ui::Color color) {
  if (text == nullptr) return;
  const float size = std::max(1.0f, std::round(fontSize));
  const float boxWidth = size * TEXT_BOX_WIDTH_FACTOR;
  const float boxHeight = size * 2.0f;
  ui::setStyleFontSize(frame.style, static_cast<int32_t>(size));
  ui::setGuiContentColor(color);
  ui::label({centerX - boxWidth * 0.5f, centerY - boxHeight * 0.5f, boxWidth, boxHeight}, text,
            frame.style);
}

inline ui::ManagedString healthText(int32_t hp, int32_t maxHp) {
  return ui::concat(ui::intToString(std::max(0, hp)), ui::charToString(HEALTH_SEPARATOR),
                    ui::intToString(maxHp));
}

/// HealthManager → EnemyDeathEffects → journalRecord（猎人日志条目）；没有返回 null
inline const uint8_t* journalRecordOf(const void* manager) {
  const void* effects = ui::readField<const void*>(manager, HM_DEATHEFFECTS);
  if (!ui::isAlive(effects)) return nullptr;
  const auto* record = ui::readField<const uint8_t*>(effects, EDE_JOURNALRECORD);
  return ui::isAlive(record) ? record : nullptr;
}

/// 猎人日志本地化名
inline ui::ManagedString bossName(const void* manager) {
  const uint8_t* record = journalRecordOf(manager);
  return record != nullptr ? ui::localisedToString(record + EJR_DISPLAYNAME) : nullptr;
}

inline void drawBoss(const Frame& frame, const Enemy& enemy, int32_t hp, int index) {
  const Rect bar = bossBarRect(index, frame.width, frame.height);
  const float scale = uiScale(frame.width, frame.height);
  const float centerX = bar.x + bar.width * 0.5f;
  drawBar(frame, bar, healthRatio(hp, enemy.maxHp), COLOR_BOSS_BG);
  drawText(frame, centerX, bar.y + bar.height * 0.5f, BOSS_NUMBERS_FONT * scale,
           healthText(hp, enemy.maxHp), COLOR_TEXT_DARK);
  drawText(frame, centerX, bar.y - BOSS_NAME_GAP * scale, BOSS_NAME_FONT * scale,
           bossName(enemy.manager), COLOR_TEXT_DARK);
}

/// 世界坐标 → IMGUI 坐标（原点左上）；在相机背后返回 false
inline bool project(const Frame& frame, ui::Vector3 world, float& outX, float& outY) {
  const ui::Vector3 screen = ui::worldToScreen(frame.camera, world);
  if (screen.z < 0.0f) return false;
  outX = screen.x * frame.cameraScaleX;
  outY = frame.height - screen.y * frame.cameraScaleY;
  return true;
}

/// 该世界位置附近一个世界单位对应的屏幕像素数
inline float pixelsPerUnit(const Frame& frame, ui::Vector3 world, float screenX) {
  float rightX = 0.0f;
  float rightY = 0.0f;
  if (!project(frame, {world.x + 1.0f, world.y, world.z}, rightX, rightY)) return 0.0f;
  return std::fabs(rightX - screenX);
}

inline void drawEnemy(const Frame& frame, const Enemy& enemy, int32_t hp) {
  ui::Vector3 anchor = ui::worldPositionOf(enemy.manager);
  anchor.y += ENEMY_BAR_OFFSET_UNITS;
  float x = 0.0f;
  float y = 0.0f;
  if (!project(frame, anchor, x, y)) return;
  const float ppu = pixelsPerUnit(frame, anchor, x);
  if (ppu <= 0.0f) return;
  const float ratio = healthRatio(hp, enemy.maxHp);
  const Rect bar = enemyBarRect(x, y, ppu);
  drawBar(frame, bar, ratio, COLOR_ENEMY_BG);
  const ui::Color textColor = ratio < LOW_HEALTH_RATIO ? COLOR_TEXT_WHITE : COLOR_TEXT_DARK;
  drawText(frame, x, bar.y - ENEMY_NUMBERS_GAP_UNITS * ppu, ENEMY_NUMBERS_FONT_UNITS * ppu,
           healthText(hp, enemy.maxHp), textColor);
}

inline void drawPopup(const Frame& frame, const Popup& popup) {
  const float progress = popupProgress(frame.now, popup.startedAt);
  const ui::Vector3 world{popup.origin.x, popup.origin.y + POPUP_RISE_UNITS * progress, popup.origin.z};
  float x = 0.0f;
  float y = 0.0f;
  if (!project(frame, world, x, y)) return;
  const float ppu = pixelsPerUnit(frame, world, x);
  if (ppu <= 0.0f) return;
  ui::Color color = COLOR_DAMAGE;
  color.a = 1.0f - progress;
  drawText(frame, x, y, DAMAGE_FONT_UNITS * ppu, ui::intToString(-popup.damage), color);
}

/// 敌人已销毁 / 死亡 / 血量归零 → 返回 false，调用方应 forget
inline bool readLiveHp(const Enemy& enemy, int32_t& outHp) {
  if (!ui::isAlive(enemy.manager)) return false;
  if (ui::readField<bool>(enemy.manager, HM_ISDEAD)) return false;
  outHp = ui::readField<int32_t>(enemy.manager, HM_HP);
  return outHp > 0;
}

inline Frame beginFrame(ui::ManagedObject style) {
  Frame frame{ui::unscaledTime(), static_cast<float>(ui::screenWidth()),
              static_cast<float>(ui::screenHeight()), ui::mainCamera(), 1.0f, 1.0f,
              ui::whiteTexture(), style};
  if (ui::isAlive(frame.camera)) {
    frame.cameraScaleX = frame.width / static_cast<float>(std::max(1, ui::cameraPixelWidth(frame.camera)));
    frame.cameraScaleY = frame.height / static_cast<float>(std::max(1, ui::cameraPixelHeight(frame.camera)));
  } else {
    frame.camera = nullptr;
  }
  return frame;
}

inline void drawEnemies(const Frame& frame, HudModel& model) {
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
      drawBoss(frame, enemy, hp, bossIndex++);
    } else if (frame.camera != nullptr) {
      drawEnemy(frame, enemy, hp);
    }
  }
}

inline void drawToast(const Frame& frame, const toggles::Toggles& toggles) {
  if (!toggles.isToastVisible(frame.now)) return;
  const float scale = uiScale(frame.width, frame.height);
  const float fontSize = TOAST_FONT * scale;
  const float boxWidth = frame.width;
  const float boxHeight = fontSize * 2.0f;
  ui::setStyleFontSize(frame.style, static_cast<int32_t>(std::max(1.0f, std::round(fontSize))));
  ui::setGuiContentColor(COLOR_TEXT_WHITE);
  ui::label({0.0f, TOAST_TOP * scale, boxWidth, boxHeight}, ui::asciiString(toggles.toastText()), frame.style);
}

/// OnGUI 入口：每个 IMGUI 事件都会调用，只在 Repaint 时画
inline void drawHud(HudModel& model, const toggles::Toggles& toggles) {
  if (ui::currentEventType() != ui::EVENT_TYPE_REPAINT) return;
  ImguiStateGuard guard;
  const Frame frame = beginFrame(guard.style());
  drawToast(frame, toggles);
  if (!toggles.isHudEnabled) return;
  if (SHOW_DEBUG_MARKER) {
    ui::setGuiColor(COLOR_DAMAGE);
    ui::drawTexture({DEBUG_MARKER_MARGIN, DEBUG_MARKER_MARGIN, DEBUG_MARKER_SIZE, DEBUG_MARKER_SIZE}, frame.white);
  }
  model.expire(frame.now);
  drawEnemies(frame, model);
  if (frame.camera == nullptr) return;
  for (int i = 0; i < MAX_POPUPS; ++i) {
    if (model.popups()[i].active) drawPopup(frame, model.popups()[i]);
  }
}

}  // namespace sshud::hud
