// BOSS 条的血量变化日志：只记开始跟踪、血量回升（阶段回满 / 设成占位）和占位段结束，受击掉血不记。
// 用来核对 FSM 用 SetHP 改血量的 BOSS（苍白之母等）实际设了哪些值。

#pragma once

#include "hud_model.hpp"
#include "options_menu_calls.hpp"

namespace sshud::hud {

/// 绘制前调用：按上一帧血量判断要不要记一行，然后更新 lastHp
inline void logBossHp(Enemy& enemy, int32_t hp) {
  const int32_t last = enemy.lastHp;
  enemy.lastHp = hp;
  const bool isFirst = last == 0;
  const bool isRise = !isFirst && hp > last;
  const bool isPlaceholderEnd = isPlaceholderHp(last) && !isPlaceholderHp(hp);
  if (!enemy.isBossStyle || !(isFirst || isRise || isPlaceholderEnd)) return;
  char name[options::NAME_CAPACITY];
  Logging.Log("[sshud] boss %s: hp %d -> %d, max %d%s", options::nameOf(const_cast<void*>(enemy.manager), name, sizeof(name)),
              last, hp, enemy.maxHp, isPlaceholderHp(hp) ? " (placeholder, bar hidden)" : "");
}

}  // namespace sshud::hud
