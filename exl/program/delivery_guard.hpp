// 送货物品不损坏（toggles::Feature::ProtectDelivery，只在原生选项页里，默认关）。
//
// 信使送货（如美食家的肉袋）的物品有耐久：挨打、硬着陆、冲刺撞墙会掉，计时的还会随时间掉，
// 掉到 0 游戏就把任务静默判失败并收走物品。反汇编确认所有掉耐久都汇到
// DeliveryQuestItem.TakeHitForItem(item, hitEffect, amount)（HeroController.TakeDamage / DoSpecialDamage /
// DoHardLandingEffect / HeroWaterController.DoSprintBonk → TakeHit；HeroController.TickDeliveryItems 计时），
// 一次碎全部的都汇到 BreakAllInternal(withEffects, onlyTimed)（DieFromHazard → BreakAll；
// BreakAllNoEffects / BreakTimedNoEffects 没有直接调用方，是状态机反射调用的）。
// 开关打开时这两个函数直接返回：物品不掉耐久也不碎，交货仍走游戏原流程。
// 两者跳过都不需要补副作用（原函数只做特效、扣耐久、判失败、收物品）。
// 计时满了 TickDeliveryItems 会每帧重试，所以拦截日志按类型只写前 MAX_LOGGED_BLOCKS 次。

#pragma once

#include "lib.hpp"
#include "toggles.hpp"

namespace sshud::delivery {

constexpr int MAX_LOGGED_BLOCKS = 3;

inline bool isProtected() { return toggles::g_settings.isEnabled(toggles::Feature::ProtectDelivery); }

inline bool shouldLog(int& count) { return count++ < MAX_LOGGED_BLOCKS; }

}  // namespace sshud::delivery

// IL2CPP 签名：static void (DeliveryQuestItem.ActiveItem item, bool hitEffect, int amount, const MethodInfo*)
//   ActiveItem 是 24 字节的值类型（Item, Quest, CurrentCount, MaxCount），经指针传（x0）。
HOOK_DEFINE_TRAMPOLINE(DeliveryTakeHitHook) {
  static void Callback(const void* item, bool hitEffect, int32_t amount, const void* method) {
    static int s_blocked = 0;
    if (!sshud::delivery::isProtected()) return Orig(item, hitEffect, amount, method);
    if (sshud::delivery::shouldLog(s_blocked)) {
      Logging.Log("[sshud] delivery: blocked hit amount=%d effect=%d", amount, hitEffect ? 1 : 0);
    }
  }
};

// IL2CPP 签名：static void (bool withEffects, bool onlyTimed, const MethodInfo*)
HOOK_DEFINE_TRAMPOLINE(DeliveryBreakAllHook) {
  static void Callback(bool withEffects, bool onlyTimed, const void* method) {
    static int s_blocked = 0;
    if (!sshud::delivery::isProtected()) return Orig(withEffects, onlyTimed, method);
    if (sshud::delivery::shouldLog(s_blocked)) {
      Logging.Log("[sshud] delivery: blocked break-all effects=%d onlyTimed=%d", withEffects ? 1 : 0,
                  onlyTimed ? 1 : 0);
    }
  }
};

namespace sshud::delivery {

inline void installHooks() {
  DeliveryTakeHitHook::InstallAtOffset(DQI_TAKEHITFORITEM);
  DeliveryBreakAllHook::InstallAtOffset(DQI_BREAKALLINTERNAL);
  Logging.Log("[sshud] delivery hooks installed: TakeHitForItem, BreakAllInternal");
}

}  // namespace sshud::delivery
