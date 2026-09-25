// HUD 用到的托管字符串：血量 "hp/maxHp"、BOSS 名、伤害数字。
//
// 每次 Repaint 都重建会产生 GC 垃圾（Int32.ToString + Concat），BOSS 名还要查语言表，
// 所以按 HudModel 的槽位各缓存一份：key 不变就复用，变了才重建。
//   血量 key = (hp, maxHp)、伤害 key = damage —— 文字只由 key 决定，槽位换了敌人也不会显示错；
//   BOSS 名 key = HealthManager 指针（与 HudModel 判断"同一个敌人"的标准一致）。
// 缓存的字符串跨帧持有，必须钉住（本模块静态区 GC 扫不到）；换掉或槽位空出来时放开。

#pragma once

#include "hud_model.hpp"
#include "unity_api.hpp"

#include <algorithm>

namespace sshud::hud {

namespace ui = sshud::unity;

constexpr char16_t HEALTH_SEPARATOR = u'/';

inline ui::ManagedString healthText(int32_t hp, int32_t maxHp) {
  return ui::concat(ui::intToString(hp), ui::charToString(HEALTH_SEPARATOR), ui::intToString(maxHp));
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

/// 一份钉住的缓存字符串（build() 返回 null 时不缓存，下次 Repaint 再试，同旧行为）
struct CachedText {
  uint64_t key = 0;
  bool isSet = false;
  ui::ManagedString text = nullptr;
  ui::PinHandle pin = 0;
};

inline void releaseText(CachedText& cached) {
  ui::unpinObject(cached.pin);
  cached = CachedText{};
}

/// key 与缓存一致就直接返回；否则放开旧串，用 build() 建新串并钉住
template <typename Build>
ui::ManagedString cachedText(CachedText& cached, uint64_t key, Build build) {
  if (cached.isSet && cached.key == key) return cached.text;
  releaseText(cached);
  cached.text = build();
  if (cached.text == nullptr) return nullptr;
  cached.pin = ui::pinObject(cached.text);
  cached.key = key;
  cached.isSet = true;
  return cached.text;
}

/// 按 HudModel 槽位下标平行排布
struct HudTextCache {
  CachedText health[MAX_ENEMIES];
  CachedText bossName[MAX_ENEMIES];
  CachedText damage[MAX_POPUPS];
};

inline HudTextCache g_textCache;

inline ui::ManagedString healthTextFor(int slot, int32_t hp, int32_t maxHp) {
  const int32_t shownHp = std::max(0, hp);
  return cachedText(g_textCache.health[slot], healthTextKey(shownHp, maxHp),
                    [&] { return healthText(shownHp, maxHp); });
}

inline ui::ManagedString bossNameFor(int slot, const void* manager) {
  return cachedText(g_textCache.bossName[slot], reinterpret_cast<uintptr_t>(manager),
                    [&] { return bossName(manager); });
}

inline ui::ManagedString damageTextFor(int slot, int32_t damage) {
  return cachedText(g_textCache.damage[slot], static_cast<uint32_t>(damage),
                    [&] { return ui::intToString(-damage); });
}

/// 槽位空出来（过期 / forget / 读血失败）就放开它的缓存串；每次 Repaint 调一次，空槽只是几次比较
inline void releaseInactiveTexts(const HudModel& model) {
  for (int i = 0; i < MAX_ENEMIES; ++i) {
    if (model.enemies()[i].active) continue;
    releaseText(g_textCache.health[i]);
    releaseText(g_textCache.bossName[i]);
  }
  for (int i = 0; i < MAX_POPUPS; ++i) {
    if (!model.popups()[i].active) releaseText(g_textCache.damage[i]);
  }
}

}  // namespace sshud::hud
