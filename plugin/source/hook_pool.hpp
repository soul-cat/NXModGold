// trampoline 存储池。
//
// 背景：Switch 内核不给 RWX，`svcCreateCodeMemory` 零售游戏的 NPDM 里没有，
// 所以我们拿不到任何"运行时分配的可执行内存"。而真正的 trampoline hook
// 必须把重定位后的原指令放在某块可执行内存里。
//
// 解法：在插件自己的 .text 段里编译期预留一块池子。插件段由 saltysd_proc
// 在加载时按 ELF phdr 标志映射成 RX，所以池子天然可执行；而 SaltySD_Memcpy
// 走 debug SVC 写入，完全绕过页权限，因此能往这块 RX 内存里填内容。
//
// 代价：hook 数量必须在编译期确定。对本项目无所谓——我们就 hook 一两个函数。
//
// ⚠️ 这条路在 SaltyNX 生态里没有先例（NX-FPS / lsfg-nx 全是 GOT 导入表替换，
//    不需要 trampoline）。实机首次运行即为验证点。

#ifndef SS_HOOK_POOL_HPP
#define SS_HOOK_POOL_HPP

#include <stdint.h>

#include "arm64_reloc.hpp"

namespace sshud {

/// 编译期确定的 hook 数量上限。
/// 1 = HealthManager::TakeDamage，留 1 个余量给 Hit（TakeDamage 报 RVA -1 时的退路）。
constexpr int MAX_HOOKS = 2;

/// 池中每个槽位的字数。
constexpr int POOL_SLOT_WORDS = arm64::TRAMPOLINE_WORDS;

/// 池子的填充值：`ret`（0xD65F03C0）。
/// 万一某个槽位没被正确填充就被跳进去，至少是干净返回，而不是执行垃圾数据。
constexpr uint32_t POOL_FILLER = 0xD65F03C0u;

/// 安装一个 hook 所需的全部计算结果。
///
/// 刻意不含任何内存写入——这样整个决策过程可以在宿主机上测试，
/// 实机侧只剩两次 SaltySD_Memcpy。
struct HookPlan {
  bool valid;                                  ///< false 表示不可安装，禁止写入任何内存
  uint64_t targetAddr;                         ///< 被 hook 的函数地址（main_base + RVA）
  uint64_t trampolineAddr;                     ///< trampoline 槽位地址（插件 .text 内）
  uint32_t trampoline[arm64::TRAMPOLINE_WORDS];///< 写进槽位的内容
  uint32_t detour[arm64::ABS_JUMP_WORDS];      ///< 写进函数开头的内容
  const char* failReason;                      ///< valid==false 时说明原因，便于诊断
};

/// 规划一次 hook 安装。
///
/// @param targetAddr      被 hook 函数的运行时地址
/// @param originalInsns   该函数开头 STOLEN_INSN_COUNT 条原始指令
/// @param slotAddr        分配给它的 trampoline 槽位的运行时地址
/// @param detourTarget    我们自己的 hook 函数地址
///
/// 失败情形（全部返回 valid=false，绝不产生半成品）：
///   - 任一参数为 0 / nullptr
///   - 目标地址未 4 字节对齐
///   - 序言里含有搬不动的指令（B.cond / CBZ / TBZ / LDR-literal 等）
///   - 序言里的 PC 相对指令搬到槽位后立即数溢出
inline HookPlan planHook(uint64_t targetAddr, const uint32_t* originalInsns,
                         uint64_t slotAddr, uint64_t detourTarget) {
  HookPlan plan = {};
  plan.valid = false;
  plan.targetAddr = targetAddr;
  plan.trampolineAddr = slotAddr;

  if (targetAddr == 0) {
    plan.failReason = "目标地址为 0（模块基址或 RVA 没拿到）";
    return plan;
  }
  if (originalInsns == nullptr) {
    plan.failReason = "原始指令未读取";
    return plan;
  }
  if (slotAddr == 0) {
    plan.failReason = "trampoline 槽位地址为 0";
    return plan;
  }
  if (detourTarget == 0) {
    plan.failReason = "hook 函数地址为 0";
    return plan;
  }
  if ((targetAddr & 3u) != 0 || (slotAddr & 3u) != 0) {
    plan.failReason = "地址未 4 字节对齐";
    return plan;
  }

  if (!arm64::buildTrampoline(originalInsns, targetAddr, slotAddr, plan.trampoline)) {
    plan.failReason = "函数序言含不可重定位指令，放弃安装";
    return plan;
  }

  arm64::makeAbsoluteJump(detourTarget, plan.detour);
  plan.valid = true;
  plan.failReason = nullptr;
  return plan;
}

}  // namespace sshud

#endif  // SS_HOOK_POOL_HPP
