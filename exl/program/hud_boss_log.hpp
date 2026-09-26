// BOSS 条的血量跟踪日志：BOSS 的血量或显示状态每变一次记一行（受击也记），用来核对 FSM 分阶段设血量的
// BOSS（苍白之母等）实际的血量走向。日志先进内存缓冲、每 2 秒整批写 SD，一场战斗几百行，不卡。

#pragma once

#include "hud_model.hpp"
#include "options_menu_calls.hpp"

namespace sshud::hud {

constexpr bool LOG_BOSS_HP = true;

/// observeHp 之后调用：previousHp / wasHidden 是这一帧之前的状态
inline void logBossHp(const Enemy& enemy, int32_t previousHp, bool wasHidden) {
  if (!LOG_BOSS_HP || !enemy.isBossStyle) return;
  if (previousHp == enemy.lastHp && wasHidden == enemy.isHidden) return;
  if (previousHp == NO_HP) {
    char name[options::NAME_CAPACITY];
    Logging.Log("[sshud] boss %p is %s", enemy.manager,
                options::nameOf(const_cast<void*>(enemy.manager), name, sizeof(name)));
  }
  Logging.Log("[sshud] boss %p hp %d -> %d max %d%s", enemy.manager, previousHp == NO_HP ? -1 : previousHp,
              enemy.lastHp, enemy.maxHp, enemy.isHidden ? " hidden" : "");
}

}  // namespace sshud::hud
