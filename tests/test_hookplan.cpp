// hook 规划逻辑测试。
// 把"能不能装 hook"的全部决策做成纯函数，就是为了能在宿主机上测到这一层——
// 实机侧只剩两次 SaltySD_Memcpy，不留判断逻辑。

#include <cstdio>
#include <cstring>

#include "../plugin/source/hook_pool.hpp"

using namespace sshud;
using namespace sshud::arm64;

static int gFailures = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    if (!(cond)) {                                                  \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
      ++gFailures;                                                  \
    }                                                               \
  } while (0)

// 由系统汇编器验证过的真实编码（见 tests/gen_ground_truth.sh）
constexpr uint32_t STP_PROLOGUE = 0xA9BE7BFDu;  // stp x29, x30, [sp, #-0x20]!
constexpr uint32_t MOV_X29_SP   = 0x910003FDu;  // mov x29, sp
constexpr uint32_t SUB_SP_0x40  = 0xD10103FFu;  // sub sp, sp, #0x40
constexpr uint32_t CBZ_X0       = 0xB4000200u;  // cbz x0, .+0x40   <- 不可重定位
constexpr uint32_t RET          = 0xD65F03C0u;  // ret

// 模拟实机布局：游戏 main NSO 在 0x71xxxxxxxx，插件被映射到别处
constexpr uint64_t MAIN_BASE = 0x7100000000ull;
constexpr uint64_t TAKEDAMAGE_RVA = 0x1E4D118ull;
constexpr uint64_t PLUGIN_TEXT = 0x7200000000ull;
constexpr uint64_t HOOK_FN = PLUGIN_TEXT + 0x800ull;

static const uint32_t kTypicalPrologue[STOLEN_INSN_COUNT] = {
    STP_PROLOGUE, MOV_X29_SP, SUB_SP_0x40, MOV_X29_SP};

static void testHappyPath() {
  std::printf("test: 正常安装路径\n");
  const uint64_t target = MAIN_BASE + TAKEDAMAGE_RVA;
  const HookPlan plan = planHook(target, kTypicalPrologue, PLUGIN_TEXT, HOOK_FN);

  CHECK(plan.valid);
  CHECK(plan.failReason == nullptr);
  CHECK(plan.targetAddr == target);
  CHECK(plan.trampolineAddr == PLUGIN_TEXT);

  // detour 必须是 ldr x17,#8 / br x17 / <HOOK_FN>
  CHECK(plan.detour[0] == INSN_LDR_X17_PC8);
  CHECK(plan.detour[1] == INSN_BR_X17);
  uint64_t detourTarget = 0;
  std::memcpy(&detourTarget, &plan.detour[2], sizeof(detourTarget));
  CHECK(detourTarget == HOOK_FN);

  // trampoline 前 4 条是搬过来的原指令（本例全是 PC 无关指令，应原样）
  for (int i = 0; i < STOLEN_INSN_COUNT; ++i)
    CHECK(plan.trampoline[i] == kTypicalPrologue[i]);

  // 之后跳回 target + 16
  uint64_t back = 0;
  std::memcpy(&back, &plan.trampoline[STOLEN_INSN_COUNT + 2], sizeof(back));
  CHECK(back == target + STOLEN_INSN_COUNT * 4);
}

static void testRejectsUnrelocatablePrologue() {
  std::printf("test: 序言含 CBZ 时拒绝安装\n");
  uint32_t prologue[STOLEN_INSN_COUNT];
  std::memcpy(prologue, kTypicalPrologue, sizeof(prologue));
  prologue[2] = CBZ_X0;

  const HookPlan plan = planHook(MAIN_BASE + TAKEDAMAGE_RVA, prologue, PLUGIN_TEXT, HOOK_FN);
  CHECK(!plan.valid);
  CHECK(plan.failReason != nullptr);
}

static void testRejectsBadInputs() {
  std::printf("test: 非法输入一律拒绝\n");
  const uint64_t target = MAIN_BASE + TAKEDAMAGE_RVA;

  CHECK(!planHook(0, kTypicalPrologue, PLUGIN_TEXT, HOOK_FN).valid);       // 目标为 0
  CHECK(!planHook(target, nullptr, PLUGIN_TEXT, HOOK_FN).valid);           // 指令未读
  CHECK(!planHook(target, kTypicalPrologue, 0, HOOK_FN).valid);            // 槽位为 0
  CHECK(!planHook(target, kTypicalPrologue, PLUGIN_TEXT, 0).valid);        // hook 函数为 0
  CHECK(!planHook(target + 1, kTypicalPrologue, PLUGIN_TEXT, HOOK_FN).valid);  // 未对齐
  CHECK(!planHook(target, kTypicalPrologue, PLUGIN_TEXT + 2, HOOK_FN).valid);  // 槽位未对齐

  // 每条拒绝都必须带原因，否则诊断没法用
  CHECK(planHook(0, kTypicalPrologue, PLUGIN_TEXT, HOOK_FN).failReason != nullptr);
  CHECK(planHook(target + 1, kTypicalPrologue, PLUGIN_TEXT, HOOK_FN).failReason != nullptr);
}

static void testFailedPlanLeavesNoPartialOutput() {
  std::printf("test: 失败时不得产生半成品\n");
  uint32_t prologue[STOLEN_INSN_COUNT];
  std::memcpy(prologue, kTypicalPrologue, sizeof(prologue));
  prologue[0] = CBZ_X0;

  const HookPlan plan = planHook(MAIN_BASE + TAKEDAMAGE_RVA, prologue, PLUGIN_TEXT, HOOK_FN);
  CHECK(!plan.valid);
  // detour 必须整块为零 —— 调用方若误写，写进去的是全 0 而不是一条"半对"的跳转
  for (int i = 0; i < ABS_JUMP_WORDS; ++i) CHECK(plan.detour[i] == 0);
}

static void testRealisticSlotLayout() {
  std::printf("test: 多槽位池布局\n");
  // 模拟一个 MAX_HOOKS 大小的池子，逐槽规划
  const uint64_t targets[MAX_HOOKS] = {MAIN_BASE + 0x1E4D118ull, MAIN_BASE + 0x1E4C3A0ull};
  for (int i = 0; i < MAX_HOOKS; ++i) {
    const uint64_t slot = PLUGIN_TEXT + (uint64_t)(i * POOL_SLOT_WORDS * 4);
    const HookPlan plan = planHook(targets[i], kTypicalPrologue, slot, HOOK_FN);
    CHECK(plan.valid);
    CHECK(plan.trampolineAddr == slot);
    uint64_t back = 0;
    std::memcpy(&back, &plan.trampoline[STOLEN_INSN_COUNT + 2], sizeof(back));
    CHECK(back == targets[i] + STOLEN_INSN_COUNT * 4);
  }
  // 槽位之间不得重叠
  CHECK(POOL_SLOT_WORDS * 4 >= (int)sizeof(uint32_t) * TRAMPOLINE_WORDS);
}

static void testPoolFillerIsRet() {
  std::printf("test: 池填充值必须是 ret\n");
  // 填充值写错会让未初始化的槽位执行垃圾数据
  CHECK(POOL_FILLER == RET);
}

int main() {
  std::printf("=== hook 规划测试 ===\n");
  testHappyPath();
  testRejectsUnrelocatablePrologue();
  testRejectsBadInputs();
  testFailedPlanLeavesNoPartialOutput();
  testRealisticSlotLayout();
  testPoolFillerIsRet();

  if (gFailures) {
    std::printf("\n%d 项失败\n", gFailures);
    return 1;
  }
  std::printf("\n全部通过\n");
  return 0;
}
