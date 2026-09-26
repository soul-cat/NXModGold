// 血条 HUD 的纯逻辑：跟踪受击敌人、伤害飘字、布局计算。
//
// 不依赖 Unity / exlaunch，宿主机可直接单测（tests/test_hud_model.cpp）。
// 数值默认对齐 PC 版 XiaoHaiiOvO/Silksong_HealthBar（见 docs/tasks/hud-draw.md）。

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sshud::hud {

// ---------------------------------------------------------------- PC 默认值
constexpr int32_t BOSS_HP_THRESHOLD = 105;   // 初始最大血量 > 此值 → BOSS 样式
// 高于此值的血量是 FSM（SetHP）设的占位值：BOSS 过场 / 无敌段先设成大数，开打再设真实血量（苍白之母即如此；
// PC 版 Silk_HpBarPro 同样只接受 ≤ 3000 的血量作上限）。占位血量不计入上限，期间不画条。
constexpr int32_t PLACEHOLDER_HP_MIN = 3001;
constexpr int32_t NO_HP = INT32_MIN;  // Enemy.lastHp：还没读过血量
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

constexpr float FONT_SIZE_STEP = 2.0f;         // 字号按 2px 取整：IMGUI 动态字体每遇到新字号都要重建字形图集
constexpr float MIN_FONT_SIZE = 1.0f;
constexpr float TEXTURE_RETRY_SECONDS = 2.0f;  // 贴图建失败后两次重试的最小间隔
constexpr int UINT32_BITS = 32;

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
  int32_t maxHp = 0;              // 当前阶段的上限；0 = 还没见过真实血量（只见过占位血量）
  int32_t lastHp = NO_HP;         // 上一帧读到的血量（observeHp 维护）
  float lastHitAt = 0.0f;
  bool active = false;
  bool isBossStyle = false;       // 见过的血量（含占位）超过 BOSS 门槛
  bool isHidden = false;          // BOSS 暂时不画（血量归零 / 停用 / 占位段），槽位留着等它回来
};

struct Popup {
  WorldPoint origin{};
  int32_t damage = 0;
  float startedAt = 0.0f;
  bool active = false;
};

inline bool isBoss(int32_t maxHp) { return maxHp > BOSS_HP_THRESHOLD; }

inline bool isPlaceholderHp(int32_t hp) { return hp >= PLACEHOLDER_HP_MIN; }

/// 受击时的上限候选（initHp 与打前血量）：取真实血量里较大的；两个都是占位时返回较大的占位值（仍用于判定 BOSS 样式）
inline int32_t pickMaxHp(int32_t initHp, int32_t hp) {
  if (isPlaceholderHp(initHp) == isPlaceholderHp(hp)) return std::max(initHp, hp);
  return isPlaceholderHp(initHp) ? hp : initHp;
}

/// 每帧读到的血量：超过上限就抬高；耗尽（≤ 0）或占位段之后又出现真实血量 = FSM 开始了新阶段，上限重设为它
/// （苍白之母每阶段用 SetHP 设一两百血，打空后设下一阶段）。返回上限是否变了
inline bool observeHp(Enemy& enemy, int32_t hp) {
  const int32_t last = enemy.lastHp;
  enemy.lastHp = hp;
  if (isPlaceholderHp(hp) || hp <= 0) return false;
  const bool isNewPhase = last != NO_HP && (last <= 0 || isPlaceholderHp(last));
  if (!isNewPhase && hp <= enemy.maxHp) return false;
  enemy.maxHp = hp;
  return true;
}

/// 这一帧能画：真实血量且已知上限
inline bool isDrawable(const Enemy& enemy, int32_t hp) { return !isPlaceholderHp(hp) && enemy.maxHp > 0; }

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

/// 连续变化的字号（随相机缩放）取整到 FONT_SIZE_STEP 的倍数，至少 MIN_FONT_SIZE
inline int32_t quantizeFontSize(float fontSize) {
  return static_cast<int32_t>(std::max(MIN_FONT_SIZE, std::round(fontSize / FONT_SIZE_STEP) * FONT_SIZE_STEP));
}

/// 血量文字缓存的 key：(hp, maxHp) 拼成一个 64 位数，文字只由这两个数决定
inline uint64_t healthTextKey(int32_t hp, int32_t maxHp) {
  return (static_cast<uint64_t>(static_cast<uint32_t>(hp)) << UINT32_BITS) | static_cast<uint32_t>(maxHp);
}

/// 失败重试节流：两次尝试至少间隔 TEXTURE_RETRY_SECONDS
struct RetryThrottle {
  float nextAttemptAt = 0.0f;

  bool tryAcquire(float now) {
    if (now < nextAttemptAt) return false;
    nextAttemptAt = now + TEXTURE_RETRY_SECONDS;
    return true;
  }
};

class HudModel {
 public:
  /// 记录一次掉血 / 扫到 BOSS；hp 由绘制时实时读取，这里只存上限与时间。
  /// initHp 只用来判定 BOSS 样式和普通敌人的上限：BOSS 的 initHp 可能只是预制体上的值
  /// （苍白之母 1204，实际每阶段一两百），所以 BOSS 的上限只认实际见过的血量。占位血量不计入上限。
  void onHit(const void* manager, int32_t initHp, int32_t hp, float now) {
    Enemy& slot = slotFor(manager);
    slot.manager = manager;
    slot.isBossStyle = slot.isBossStyle || isBoss(pickMaxHp(initHp, hp));
    const int32_t candidate = slot.isBossStyle ? hp : pickMaxHp(initHp, hp);
    if (!isPlaceholderHp(candidate)) slot.maxHp = std::max(slot.maxHp, candidate);
    slot.lastHitAt = now;
    slot.active = true;
  }

  void onHit(const void* manager, int32_t maxHp, float now) { onHit(manager, maxHp, maxHp, now); }

  void addPopup(WorldPoint origin, int32_t damage, float now) {
    Popup& slot = oldestPopup();
    slot = {origin, damage, now, true};
  }

  void expire(float now) {
    for (Enemy& enemy : enemies_) {
      if (enemy.active && !enemy.isBossStyle && now - enemy.lastHitAt >= HIDE_DELAY_SECONDS) enemy = Enemy{};
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

  /// 没有要画的敌人 / 飘字（OnGUI 可以直接返回）
  bool isEmpty() const {
    const auto isActive = [](const auto& slot) { return slot.active; };
    return std::none_of(enemies_, enemies_ + MAX_ENEMIES, isActive) &&
           std::none_of(popups_, popups_ + MAX_POPUPS, isActive);
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
