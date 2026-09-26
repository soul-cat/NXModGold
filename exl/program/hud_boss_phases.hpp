// 分段 BOSS 的段表：从 BOSS 自己的 PlayMaker FSM「Phase Control」读 int 变量 P1 HP … Pn HP。
//
// 苍白之母（cradle_03 的 Silk Boss，资源包解出来核对）：Phase Control 开场 SetHP(P1 HP)，之后每段 CompareHP ≤ 0
// 就 AddHP(下一段)，共 6 段 100/192/240/110/242/340 = 1224（initHp 1204 只是预制体上的值）；
// 是「加」不是「设」，打穿的溢出伤害会从下一段扣掉，所以 剩余 = 当前血量 + 后面各段。
// 全是字段读取（PlayMakerFSM.fsmList → fsm → name / variables.intVariables），只有比对 GameObject 用一次托管调用；
// 每个 BOSS 只在判定为分段时读一次。

#pragma once

#include "boss_detect.hpp"
#include "hud_model.hpp"
#include "options_menu_calls.hpp"

#include <cstring>

namespace sshud::hud {

namespace ui = sshud::unity;

constexpr const char* PHASE_FSM_NAME = "Phase Control";

/// 托管字符串 → ASCII 后与 expected 比较
inline bool isNamed(ui::ManagedString text, const char* expected) {
  char ascii[options::NAME_CAPACITY];
  return std::strcmp(options::asciiOf(text, ascii, sizeof(ascii)), expected) == 0;
}

/// Fsm.variables.intVariables 里的 "P<n> HP" 填进段表；编号不连续返回空表
inline PhasePlan phasePlanOf(const void* fsm) {
  PhasePlan plan;
  const void* variables = ui::readField<const void*>(fsm, FSM_VARIABLES);
  const void* ints = variables != nullptr ? ui::readField<const void*>(variables, FSMVARS_INTVARIABLES) : nullptr;
  const uint64_t count = ints != nullptr ? map::arrayLength(ints) : 0;
  for (uint64_t i = 0; i < count; ++i) {
    const void* variable = map::arrayItem(ints, i);
    if (variable == nullptr) continue;
    char name[options::NAME_CAPACITY];
    const int number = phaseNumberOf(options::asciiOf(ui::readField<ui::ManagedString>(variable, NAMEDVAR_NAME), name, sizeof(name)));
    if (number == 0) continue;
    plan.hp[number - 1] = ui::readField<int32_t>(variable, FSMINT_VALUE);
    plan.count = std::max(plan.count, number);
  }
  return plan;
}

/// manager 所在 GameObject 上名为 Phase Control 的 FSM；没有返回 null
inline const void* phaseFsmOf(const void* manager) {
  const void* statics = staticFieldsOf(PMFSM_TYPEINFO);
  const void* list = statics != nullptr ? ui::readField<const void*>(statics, PMFSM_FSMLIST_STATIC) : nullptr;
  const void* items = list != nullptr ? ui::readField<const void*>(list, IL2CPP_LIST_ITEMS) : nullptr;
  if (items == nullptr) return nullptr;
  const void* owner = map::gameObjectOf(const_cast<void*>(manager));
  const int32_t size = ui::readField<int32_t>(list, IL2CPP_LIST_SIZE);
  const auto capacity = static_cast<int32_t>(ui::readField<uint64_t>(items, IL2CPP_ARRAY_LENGTH_FIELD));
  for (int32_t i = 0; i < size && i < capacity; ++i) {
    void* component = ui::readField<void*>(items, IL2CPP_ARRAY_FIRST_ITEM + i * sizeof(void*));
    const void* fsm = ui::isAlive(component) ? ui::readField<const void*>(component, PMFSM_FSM) : nullptr;
    if (fsm == nullptr || !isNamed(ui::readField<ui::ManagedString>(fsm, FSM_NAME), PHASE_FSM_NAME)) continue;
    if (map::gameObjectOf(component) == owner) return fsm;  // 名字对上了才做托管调用
  }
  return nullptr;
}

/// 判定为分段 BOSS 时调用一次：段表可用（isChunkPlan）就返回它，否则空表（退回 initHp − 累计掉血）
inline PhasePlan readPhasePlan(const void* manager, int32_t initHp) {
  const void* fsm = phaseFsmOf(manager);
  const PhasePlan plan = fsm != nullptr ? phasePlanOf(fsm) : PhasePlan{};
  const bool isUsable = isChunkPlan(plan, initHp);
  Logging.Log("[sshud] boss %p phases: fsm=%d count=%d total=%d initHp=%d -> %s", manager, fsm != nullptr ? 1 : 0,
              plan.count, phaseSum(plan, 0), initHp, isUsable ? "per-phase" : "initHp");
  return isUsable ? plan : PhasePlan{};
}

}  // namespace sshud::hud
