// 血条 HUD 的纯逻辑：跟踪受击敌人、伤害飘字、布局计算。
//
// 不依赖 Unity / exlaunch，宿主机可直接单测（tests/test_hud_model.cpp）。
// 数值默认对齐 PC 版 XiaoHaiiOvO/Silksong_HealthBar（见 docs/tasks/hud-draw.md）。

#pragma once

#include <algorithm>
#include <cstdint>

namespace sshud::hud {

// ---------------------------------------------------------------- PC 默认值
constexpr int32_t BOSS_HP_THRESHOLD = 105;   // 初始最大血量 > 此值 → BOSS 样式
constexpr float HIDE_DELAY_SECONDS = 1.5f;   // 普通敌人最后一次掉血后多久隐藏（BOSS 条不按时间隐藏）
constexpr float POPUP_DURATION_SECONDS = 2.0f;
constexpr float POPUP_RISE_UNITS = 1.4f;     // 飘字上升的世界单位
constexpr float POPUP_START_OFFSET_UNITS = 1.0f;

constexpr float REFERENCE_WIDTH = 1920.0f;   // CanvasScaler 参考分辨率
constexpr float REFERENCE_HEIGHT = 1080.0f;
constexpr float BOSS_BAR_WIDTH = 900.0f;
constexpr float BOSS_BAR_HEIGHT = 36.0f;  // 贴图外框高度（PC 纯色条是 25，贴图带描边尖角需要更高）
constexpr float BOSS_BAR_EDGE_MARGIN = 42.0f;  // 距屏幕底边
constexpr float BOSS_BAR_STACK_GAP = 54.0f;    // 多个 BOSS 纵向间距 = 高 + 54
constexpr float BAR_FILL_INSET = 2.0f;

constexpr float ENEMY_BAR_WIDTH_UNITS = 1.35f;   // 世界空间：135 × 缩放 0.01
constexpr float ENEMY_BAR_HEIGHT_UNITS = 0.25f;
constexpr float ENEMY_BAR_OFFSET_UNITS = 1.5f;   // 敌人原点上方
constexpr float LOW_HEALTH_RATIO = 0.49f;        // 低于此比例数字改白色

constexpr int MAX_ENEMIES = 16;
constexpr int MAX_POPUPS = 32;

struct Rect {
  float x;
  float y;
  float width;
  float height;
};

struct WorldPoint {
  float x;
  float y;
  float z;
};

struct Enemy {
  const void* manager = nullptr;  // HealthManager*（仅作标识与读字段，不持有 GC 引用）
  int32_t maxHp = 0;
  float lastHitAt = 0.0f;
  bool active = false;
};

struct Popup {
  WorldPoint origin{};
  int32_t damage = 0;
  float startedAt = 0.0f;
  bool active = false;
};

inline bool isBoss(int32_t maxHp) { return maxHp > BOSS_HP_THRESHOLD; }

inline float healthRatio(int32_t hp, int32_t maxHp) {
  if (maxHp <= 0) return 0.0f;
  return std::clamp(static_cast<float>(hp) / static_cast<float>(maxHp), 0.0f, 1.0f);
}

/// 等比缩放（取宽高较小的比例，保证不出屏）
inline float uiScale(float screenWidth, float screenHeight) {
  return std::min(screenWidth / REFERENCE_WIDTH, screenHeight / REFERENCE_HEIGHT);
}

/// 第 index 个可见 BOSS 条（IMGUI 坐标：原点左上，y 向下）。底部居中，向上堆叠。
inline Rect bossBarRect(int index, float screenWidth, float screenHeight) {
  const float scale = uiScale(screenWidth, screenHeight);
  const float width = BOSS_BAR_WIDTH * scale;
  const float height = BOSS_BAR_HEIGHT * scale;
  const float bottom =
      screenHeight - (BOSS_BAR_EDGE_MARGIN + index * (BOSS_BAR_HEIGHT + BOSS_BAR_STACK_GAP)) * scale;
  return {(screenWidth - width) * 0.5f, bottom - height, width, height};
}

/// 背景内缩后按比例截取的填充区
inline Rect fillRect(const Rect& bar, float ratio, float inset) {
  const float innerWidth = std::max(0.0f, bar.width - inset * 2.0f);
  const float innerHeight = std::max(0.0f, bar.height - inset * 2.0f);
  return {bar.x + inset, bar.y + inset, innerWidth * std::clamp(ratio, 0.0f, 1.0f), innerHeight};
}

/// 以 (centerX, bottomY) 为锚、给定像素/世界单位比例的普通敌人血条
inline Rect enemyBarRect(float centerX, float bottomY, float pixelsPerUnit) {
  const float width = ENEMY_BAR_WIDTH_UNITS * pixelsPerUnit;
  const float height = ENEMY_BAR_HEIGHT_UNITS * pixelsPerUnit;
  return {centerX - width * 0.5f, bottomY - height, width, height};
}

inline float popupProgress(float now, float startedAt) {
  return std::clamp((now - startedAt) / POPUP_DURATION_SECONDS, 0.0f, 1.0f);
}

class HudModel {
 public:
  /// 记录一次掉血；hp 由绘制时实时读取，这里只存最大血量与时间。
  void onHit(const void* manager, int32_t maxHp, float now) {
    Enemy& slot = slotFor(manager);
    slot.manager = manager;
    slot.maxHp = std::max(slot.maxHp, maxHp);
    slot.lastHitAt = now;
    slot.active = true;
  }

  void addPopup(WorldPoint origin, int32_t damage, float now) {
    Popup& slot = oldestPopup();
    slot = {origin, damage, now, true};
  }

  void expire(float now) {
    for (Enemy& enemy : enemies_) {
      if (enemy.active && !isBoss(enemy.maxHp) && now - enemy.lastHitAt >= HIDE_DELAY_SECONDS) enemy = Enemy{};
    }
    for (Popup& popup : popups_) {
      if (popup.active && now - popup.startedAt >= POPUP_DURATION_SECONDS) popup = Popup{};
    }
  }

  void forget(const void* manager) {
    for (Enemy& enemy : enemies_) {
      if (enemy.manager == manager) enemy = Enemy{};
    }
  }

  Enemy* enemies() { return enemies_; }
  const Enemy* enemies() const { return enemies_; }
  const Popup* popups() const { return popups_; }

 private:
  /// 已跟踪 → 复用；否则空槽；否则淘汰最久未受击的
  Enemy& slotFor(const void* manager) {
    Enemy* oldest = &enemies_[0];
    for (Enemy& enemy : enemies_) {
      if (enemy.active && enemy.manager == manager) return enemy;
    }
    for (Enemy& enemy : enemies_) {
      if (!enemy.active) return enemy;
      if (enemy.lastHitAt < oldest->lastHitAt) oldest = &enemy;
    }
    *oldest = Enemy{};
    return *oldest;
  }

  Popup& oldestPopup() {
    Popup* oldest = &popups_[0];
    for (Popup& popup : popups_) {
      if (!popup.active) return popup;
      if (popup.startedAt < oldest->startedAt) oldest = &popup;
    }
    return *oldest;
  }

  Enemy enemies_[MAX_ENEMIES]{};
  Popup popups_[MAX_POPUPS]{};
};

}  // namespace sshud::hud
