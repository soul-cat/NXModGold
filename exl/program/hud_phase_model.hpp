// 分段 BOSS 段表的纯逻辑（不依赖 Unity，宿主机单测 tests/test_hud_model.cpp）。
// 段表从 BOSS 的 FSM「Phase Control」读（hud_boss_phases.hpp）。

#pragma once

#include <cstdint>

namespace sshud::hud {

constexpr int MAX_PHASES = 8;
constexpr int32_t PHASE_TOTAL_TOLERANCE_PERCENT = 10;
constexpr int32_t PERCENT = 100;

/// 分段 BOSS 的各段血量：FSM「Phase Control」的 int 变量 P1 HP … Pn HP（苍白之母 100/192/240/110/242/340）
struct PhasePlan {
  int32_t hp[MAX_PHASES] = {};
  int count = 0;
};

/// FSM 变量名 "P<n> HP" → n（1..MAX_PHASES）；别的名字返回 0
inline int phaseNumberOf(const char* name) {
  if (name[0] != 'P' || name[1] < '1' || name[1] > '9') return 0;
  const int number = name[1] - '0';
  const bool isPhaseHp = name[2] == ' ' && name[3] == 'H' && name[4] == 'P' && name[5] == '\0';
  return isPhaseHp && number <= MAX_PHASES ? number : 0;
}

inline int32_t phaseSum(const PhasePlan& plan, int from) {
  int32_t sum = 0;
  for (int i = from; i < plan.count; ++i) sum += plan.hp[i];
  return sum;
}

/// 段表是「打空一段加下一段」（AddHP）的那种：至少两段、每段为正、合计与 initHp 相差不超过 10%
/// （苍白之母 1224 vs 1204）。阈值式阶段（P2 HP = 剩多少血进二阶段）合计对不上 initHp
inline bool isChunkPlan(const PhasePlan& plan, int32_t initHp) {
  if (plan.count < 2 || initHp <= 0) return false;
  for (int i = 0; i < plan.count; ++i) {
    if (plan.hp[i] <= 0) return false;
  }
  const int32_t diff = phaseSum(plan, 0) - initHp;
  return (diff < 0 ? -diff : diff) * PERCENT <= initHp * PHASE_TOTAL_TOLERANCE_PERCENT;
}

}  // namespace sshud::hud
