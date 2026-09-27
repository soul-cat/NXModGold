// PlayMaker 状态机查找：PlayMakerFSM.fsmList（静态，所有已启用的 PlayMakerFSM）里找挂在某个 GameObject 上、
// 名字（Fsm.name）是 fsmName 的那个组件。全是字段读取，只有名字对上时才做一次托管调用比对 GameObject。
// 用处：BOSS 条读分段血量（hud_boss_phases.hpp）、BOSS 挑战给 BOSS 状态机发事件 / 跳状态（boss_rush_calls.hpp）。

#pragma once

#include "boss_detect.hpp"
#include "map_calls.hpp"
#include "options_menu_calls.hpp"

#include <cstring>

namespace sshud::fsm {

namespace ui = sshud::unity;

/// 托管字符串 → ASCII 后与 expected 比较
inline bool isNamed(ui::ManagedString text, const char* expected) {
  char ascii[options::NAME_CAPACITY];
  return std::strcmp(options::asciiOf(text, ascii, sizeof(ascii)), expected) == 0;
}

/// gameObject 上名为 fsmName 的 PlayMakerFSM 组件；没有（或物体停用、FSM 未启用）返回 null
inline void* componentOn(const void* gameObject, const char* fsmName) {
  const void* statics = hud::staticFieldsOf(PMFSM_TYPEINFO);
  const void* list = statics != nullptr ? ui::readField<const void*>(statics, PMFSM_FSMLIST_STATIC) : nullptr;
  const void* items = list != nullptr ? ui::readField<const void*>(list, hud::IL2CPP_LIST_ITEMS) : nullptr;
  if (items == nullptr || gameObject == nullptr) return nullptr;
  const int32_t size = ui::readField<int32_t>(list, hud::IL2CPP_LIST_SIZE);
  const auto capacity = static_cast<int32_t>(ui::readField<uint64_t>(items, hud::IL2CPP_ARRAY_LENGTH_FIELD));
  for (int32_t i = 0; i < size && i < capacity; ++i) {
    void* component = ui::readField<void*>(items, hud::IL2CPP_ARRAY_FIRST_ITEM + i * sizeof(void*));
    const void* fsm = ui::isAlive(component) ? ui::readField<const void*>(component, PMFSM_FSM) : nullptr;
    if (fsm == nullptr || !isNamed(ui::readField<ui::ManagedString>(fsm, FSM_NAME), fsmName)) continue;
    if (map::gameObjectOf(component) == gameObject) return component;  // 名字对上了才做托管调用
  }
  return nullptr;
}

}  // namespace sshud::fsm
