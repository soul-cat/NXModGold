// BOSS 战一开场就显示 BOSS 条（不用等第一刀）。
//
// 信号：PlayMaker 动作 DisplayBossTitle.OnEnter —— BOSS 大标题出现的那一刻。
// 之后 BOSS_SCAN_SECONDS 内每帧扫一遍 HealthManager._activeHealthManagers（场上激活的敌人），
// 血量超过 BOSS 门槛、自身激活、不是把伤害转发给本体的部件，就登记进 HudModel。
// 登记后的 BOSS 条不按时间隐藏，死亡 / 停用 / 离开场景才消失（hud_draw.hpp readLiveHp）。
//
// 静态字段的读法照抄游戏自己的 HealthManager.OnEnable（反汇编核实）：
//   TypeInfo 槽 → Il2CppClass*；klass+0xE4 非 0 表示静态构造已完成；klass+0xB8 → static_fields。

#pragma once

#include "hud_model.hpp"
#include "lib.hpp"
#include "unity_api.hpp"

namespace sshud::hud {

constexpr uintptr_t IL2CPP_CLASS_STATIC_FIELDS = 0xB8;
constexpr uintptr_t IL2CPP_CLASS_CCTOR_FINISHED = 0xE4;
constexpr uintptr_t IL2CPP_LIST_ITEMS = 0x10;
constexpr uintptr_t IL2CPP_LIST_SIZE = 0x18;
constexpr uintptr_t IL2CPP_ARRAY_LENGTH_FIELD = 0x18;
constexpr uintptr_t IL2CPP_ARRAY_FIRST_ITEM = 0x20;
constexpr float BOSS_SCAN_SECONDS = 5.0f;  // 标题出现后 BOSS 可能晚一点才激活 / 落地

inline float g_bossScanUntil = 0.0f;

/// HealthManager._activeHealthManagers（List<HealthManager>）；类还没初始化时返回 null
inline const void* activeHealthManagers() {
  const auto* slot = reinterpret_cast<const void* const*>(exl::util::modules::GetTargetOffset(HM_TYPEINFO));
  const void* klass = *slot;
  if (klass == nullptr || unity::readField<int32_t>(klass, IL2CPP_CLASS_CCTOR_FINISHED) == 0) return nullptr;
  const void* statics = unity::readField<const void*>(klass, IL2CPP_CLASS_STATIC_FIELDS);
  return statics != nullptr ? unity::readField<const void*>(statics, HM_ACTIVELIST_STATIC) : nullptr;
}

/// 先查纯字段读取，最后才做两次托管调用的 isActiveInHierarchy
inline bool isBossCandidate(const void* manager) {
  if (!unity::isAlive(manager) || unity::readField<bool>(manager, HM_ISDEAD)) return false;
  if (unity::readField<const void*>(manager, HM_SENDDAMAGETO) != nullptr) return false;
  return isBoss(unity::readField<int32_t>(manager, HM_HP)) && unity::isActiveInHierarchy(manager);
}

/// 每帧调用；只在 BOSS 标题出现后的窗口期内干活
inline void scanForBosses(HudModel& model, float now) {
  if (now >= g_bossScanUntil) return;
  const void* list = activeHealthManagers();
  if (list == nullptr) return;
  const void* items = unity::readField<const void*>(list, IL2CPP_LIST_ITEMS);
  if (items == nullptr) return;
  const int32_t size = unity::readField<int32_t>(list, IL2CPP_LIST_SIZE);
  const auto capacity = static_cast<int32_t>(unity::readField<uint64_t>(items, IL2CPP_ARRAY_LENGTH_FIELD));
  for (int32_t i = 0; i < size && i < capacity; ++i) {
    const void* manager = unity::readField<const void*>(items, IL2CPP_ARRAY_FIRST_ITEM + i * sizeof(void*));
    if (isBossCandidate(manager)) model.onHit(manager, unity::readField<int32_t>(manager, HM_HP), now);
  }
}

}  // namespace sshud::hud

// IL2CPP 签名：void (DisplayBossTitle* self, const MethodInfo* method)
HOOK_DEFINE_TRAMPOLINE(BossTitleHook) {
  static void Callback(void* self, const void* method) {
    Orig(self, method);
    sshud::hud::g_bossScanUntil = sshud::unity::unscaledTime() + sshud::hud::BOSS_SCAN_SECONDS;
    Logging.Log("[sshud] boss title shown, scanning for boss health managers");
  }
};
