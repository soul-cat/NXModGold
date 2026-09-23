// SaltyNX 插件入口 —— Silksong 血条 HUD。
//
// 生命周期（见 saltysd_core/source/main.c:434-505）：
//
//   saltysd_proc 劫持游戏主线程 → bootstrap → Core main()
//     :466  SaltySDCore_LoadPlugins()   ← 本文件的 main() 在这里执行
//     :496  SaltySD_Deinit()            ← IPC 会话关闭，Memcpy 从此不可用
//     ...   __libnx_exit(0) → rtld → 游戏真正开始跑
//
// 关键推论：
//   1. 我们的 main() 跑在游戏执行任何自身代码之前。main NSO 已映射，
//      .text 字节已最终确定（重定位只动 .data/.got），所以可以安全打补丁。
//   2. 但 IL2CPP 尚未初始化 —— 没有 metadata、没有方法指针。
//      任何需要 IL2CPP 运行时状态的事都必须推迟到 hook 里做。
//      我们的 hook 体只按固定偏移读字段，不碰 IL2CPP 运行时，因此没问题。
//   3. 所有代码 patch 必须在 main() 里一次做完。
//   4. main() 必须 return，绝不能 exit —— exit 会杀掉游戏进程。

// libnx_min 运行时：只 include switch_min.h，绝不能混系统 switch.h（类型冲突）
#include <switch_min.h>

#include <cstdio>
#include <cstring>

#include "../../shared/hud_protocol.h"
#include "arm64_reloc.hpp"
#include "hook_pool.hpp"
#include "offsets.h"
#include "tracker.hpp"

// SaltyNX 的头文件来自 SaltyNX 仓库的 saltysd_core/source/
// （不要用 NX-FPS 的 include/saltysd/，那份已经过期：
//   它把 SaltySD_Init 声明成 void，实际返回 Result）
extern "C" {
#include "saltysd/saltysd_core.h"
#include "saltysd_dynamic.h"
#include "saltysd/saltysd_ipc.h"
}

// ---------------------------------------------------------------------------
// libnx 启动接管
//
// 标准 libnx crt0 在 `b main` 之前把 x30 设成 exit。若放任不管，
// main() 返回时会调 exit 杀掉整个游戏进程。
// libnx 的 crt0 把 x27（加载器返回地址）作为第 3 个参数传给 __libnx_init，
// 我们存下来，在 __libnx_exit 里跳回去。
// 这是 NX-FPS 验证过的写法（NX-FPS/source/main.cpp:71-96）。
// ---------------------------------------------------------------------------
extern "C" {

// libnx crt0/runtime 的符号（C++ 下必须显式声明，不能靠隐式声明）
extern char* fake_heap_start;
extern char* fake_heap_end;
extern void __libc_init_array(void);
extern void __libc_fini_array(void);
extern void virtmemSetup(void);
__attribute__((noreturn)) void __nx_exit(int rc, void* savedLr);

// 插件不能动游戏的堆，自己带一块。
// 我们全程不做动态分配，1 页足够 libc 初始化用。
alignas(16) static char g_fakeHeap[0x1000];

static void* g_origCtx = nullptr;
static Handle g_origMainThread = 0;
static void* g_origSavedLr = nullptr;

void __libnx_init(void* ctx, Handle mainThread, void* savedLr) {
  fake_heap_start = &g_fakeHeap[0];
  fake_heap_end = &g_fakeHeap[sizeof(g_fakeHeap)];
  g_origCtx = ctx;
  g_origMainThread = mainThread;
  g_origSavedLr = savedLr;
  __libc_init_array();
  virtmemSetup();
}

void __attribute__((weak, noreturn)) __libnx_exit(int rc) {
  (void)rc;
  __libc_fini_array();
  __nx_exit(0, g_origSavedLr);  // libnx 的 __nx_exit 执行 `br x1`
  while (true) {
  }
}

}  // extern "C"

namespace {

// ---------------------------------------------------------------------------
// trampoline 池
//
// 放在插件自己的 .text 里 —— saltysd_proc 按 ELF phdr 把该段映射为 RX，
// 所以它可执行；而 SaltySD_Memcpy 走 debug SVC 写入，绕过页权限，
// 所以我们能往这块 RX 内存里填内容。这是在拿不到 RWX 的前提下
// 做出真 trampoline 的唯一办法。
//
// 用 ret 填充：万一某槽位没被正确写入就被跳进去，干净返回而不是执行垃圾。
// volatile 防止编译器认为内容恒定而把读取优化掉。
// ---------------------------------------------------------------------------
// section flags 必须显式给 "ax"：volatile 数组默认继承 RW 属性，与 .text.*
// 名字冲突时会被链接器静默丢弃（不进任何 LOAD 段，SaltySD_Memcpy 时才炸）。
__attribute__((section(".text.sshud_pool,\"ax\""), used, aligned(16)))
volatile uint32_t g_trampolinePool[sshud::MAX_HOOKS][sshud::POOL_SLOT_WORDS] = {
    {sshud::POOL_FILLER, sshud::POOL_FILLER, sshud::POOL_FILLER, sshud::POOL_FILLER,
     sshud::POOL_FILLER, sshud::POOL_FILLER, sshud::POOL_FILLER, sshud::POOL_FILLER},
    {sshud::POOL_FILLER, sshud::POOL_FILLER, sshud::POOL_FILLER, sshud::POOL_FILLER,
     sshud::POOL_FILLER, sshud::POOL_FILLER, sshud::POOL_FILLER, sshud::POOL_FILLER},
};

sshud::EntityTracker g_tracker;
SsHudBlock* g_shared = nullptr;

/// trampoline 函数指针类型：与 HealthManager::TakeDamage 的 ARM64 ABI 一致。
/// HitInstance 是大号 by-value struct，AAPCS64 下经隐藏指针传递，
/// 所以第二个参数是指针而不是结构体本身。
using TakeDamageFn = void (*)(void* self, void* hitInstance);

TakeDamageFn g_origTakeDamage = nullptr;

/// 读 ARMv8 的虚拟计数器，用作单调时间戳。
inline uint64_t readTick() {
  uint64_t value = 0;
  __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(value));
  return value;
}

inline uint64_t readTickFreq() {
  uint64_t value = 0;
  __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(value));
  return value;
}

// ---------------------------------------------------------------------------
// hook 体
//
// 跑在游戏主线程上，每次敌人受击时触发。必须极轻：
// 无分配、无锁、无 IPC、无日志。做完就把控制权还给原函数。
// ---------------------------------------------------------------------------
void takeDamageHook(void* self, void* hitInstance) {
  if (self != nullptr && hitInstance != nullptr) {
    const auto* base = static_cast<const uint8_t*>(self);
    const auto* hit = static_cast<const uint8_t*>(hitInstance);

    // 只统计玩家造成的伤害，和 PC 版血条 mod 的过滤条件一致
    const bool isHeroDamage = *reinterpret_cast<const bool*>(hit + HIT_ISHERODAMAGE);
    if (isHeroDamage) {
      const int32_t damage = *reinterpret_cast<const int32_t*>(hit + HIT_DAMAGEDEALT);
      // 注意：此刻读到的 hp 是扣血【之前】的值，原函数还没执行。
      // 扣血后的血量用 hp - damage 推算，避免为了拿新值再 hook 一次返回点。
      const int32_t hpBefore = *reinterpret_cast<const int32_t*>(base + HM_HP);
      const int32_t maxHp = *reinterpret_cast<const int32_t*>(base + HM_INITHP);

      g_tracker.onDamage(reinterpret_cast<uint64_t>(self), hpBefore - damage, maxHp,
                         damage, 0, readTick());
      g_tracker.publish(readTick());
    }
  }

  if (g_origTakeDamage != nullptr) {
    g_origTakeDamage(self, hitInstance);
  }
}

// ---------------------------------------------------------------------------
// 启动步骤
// ---------------------------------------------------------------------------

/// 定位游戏 main NSO 的基址。
///
/// ⚠️ SaltySDCore_getCodeStart() 返回的是 rtld，不是 main
/// （saltysd_core.c:8-31，取的是第一个不属于 Core 的 RX 区域，
///   而 Core 被映射在 rtld 下方 game_start - 0x200000 处）。
///
/// 这是 NX-FPS / FPSLocker / Status-Monitor 共用的启发式做法
/// （NX-FPS/source/main.cpp:184-198）：假设 rtld 模块整体不超过 0x4000，
/// 跨过它之后逐区域找第一个起始地址正好等于探测点的可执行区域。
/// 不是 API 契约，但整个零售游戏库上都这么用。
///
/// ⚠️ 实机翻车记录（2026-09-24，Silksong v1.0.3）：Silksong 的布局里
/// 启发式命中了 main+0x200000 的可执行区域，detour 写错位置，游戏死在
/// 引擎启动前（主机 logo 后黑屏）。因此这里必须叠加【签名校验】：
/// 候选区域开头 16 字节与 SS_MAIN_SIG（转储的 main RVA0 mod 头）比对，
/// 不匹配就继续找，最多 12 个区域。宁可返回 0 也不写错位置。
uint64_t findMainNsoBase() {
  MemoryInfo info = {};
  u32 pageInfo = 0;
  uint64_t probe = SaltySDCore_getCodeStart() + 0x4000;

  for (int attempt = 0; attempt < 12; ++attempt) {
    if (R_FAILED(svcQueryMemory(&info, &pageInfo, probe))) return 0;
    if (info.addr == probe && (info.perm & Perm_X) != 0) {
#ifdef SS_MAIN_SIG0
      uint64_t sig[2] = {};
      if (R_SUCCEEDED(SaltySD_Memcpy(reinterpret_cast<uintptr_t>(sig), probe, sizeof(sig))) &&
          sig[0] == SS_MAIN_SIG0 && sig[1] == SS_MAIN_SIG1) {
        return probe;   // 签名匹配，这就是 main
      }
      // 签名不符：继续扫下一个区域（不做任何假设）
#else
      return probe;     // 无签名数据时退回原始启发式（不应发生）
#endif
    }
    probe = info.addr + info.size;
    if (probe == 0) return 0;
  }
  return 0;
}

/// 校验游戏版本与 offsets.h 是否匹配。
///
/// Build ID 一变，所有 RVA 全部作废。宁可什么都不做，也绝不能拿错误的
/// 偏移去 patch 别的版本 —— 那是直接崩游戏甚至损坏存档。
bool verifyBuildId() {
  // SaltySD_GetBID() 返回 build id 前 8 字节（u64），无参数。
  u64 buildId = SaltySD_GetBID();
  if (buildId == 0) return false;
  char buf[0x11] = {};
  std::snprintf(buf, sizeof(buf), "%016lX", buildId);
  return std::strncmp(buf, SS_BUILD_ID_STR, 16) == 0;
}

/// 申请共享内存里属于我们的一小块，并建立映射。
///
/// 这块共享内存一共只有 0x1000 字节，由所有插件共用，
/// 所以只申请 sizeof(SsHudBlock)（8 槽位 = 432 字节）。
/// 映射本身在 SaltySD_Deinit() 之后依然有效，只有 IPC 会话会关闭。
bool setupSharedMemory() {
  static SharedMemory shmem = {};
  ptrdiff_t offset = 0;
  Handle handle = 0;

  if (R_FAILED(SaltySD_CheckIfSharedMemoryAvailable(&offset, SSHUD_BLOCK_SIZE))) {
    return false;
  }
  if (R_FAILED(SaltySD_GetSharedMemoryHandle(&handle))) return false;

  shmemLoadRemote(&shmem, handle, SSHUD_SHMEM_TOTAL, Perm_Rw);
  if (R_FAILED(shmemMap(&shmem))) return false;

  g_shared = reinterpret_cast<SsHudBlock*>(
      reinterpret_cast<uintptr_t>(shmemGetAddr(&shmem)) + offset);
  return true;
}

/// 安装一个 trampoline hook。
///
/// 两次 SaltySD_Memcpy：先填 trampoline 槽位，再改写函数开头。
/// 顺序不能反 —— 必须保证 detour 生效时 trampoline 已经就位，
/// 否则中间窗口里被调用会跳进填充的 ret，行为虽安全但会丢事件。
bool installHook(uint64_t targetAddr, int slotIndex, void* hookFn,
                 TakeDamageFn* outOriginal) {
  if (slotIndex < 0 || slotIndex >= sshud::MAX_HOOKS) return false;

  const auto slotAddr =
      reinterpret_cast<uint64_t>(const_cast<uint32_t*>(&g_trampolinePool[slotIndex][0]));

  // 读出原函数开头的指令。用 SaltySD_Memcpy 而非直接解引用，
  // 保持与写入同一条路径，避免缓存视图不一致。
  uint32_t original[sshud::arm64::STOLEN_INSN_COUNT] = {};
  if (R_FAILED(SaltySD_Memcpy(reinterpret_cast<uintptr_t>(original), targetAddr,
                              sizeof(original)))) {
    return false;
  }

  const sshud::HookPlan plan = sshud::planHook(
      targetAddr, original, slotAddr, reinterpret_cast<uint64_t>(hookFn));
  if (!plan.valid) {
    SaltySD_printf("sshud: 放弃安装 hook @ %lx — %s\n", targetAddr, plan.failReason);
    return false;
  }

  if (R_FAILED(SaltySD_Memcpy(slotAddr, reinterpret_cast<uintptr_t>(plan.trampoline),
                              sizeof(plan.trampoline)))) {
    return false;
  }
  if (R_FAILED(SaltySD_Memcpy(targetAddr, reinterpret_cast<uintptr_t>(plan.detour),
                              sizeof(plan.detour)))) {
    return false;
  }

  *outOriginal = reinterpret_cast<TakeDamageFn>(slotAddr);
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// 分阶段调试开关（实机二分定位用）：
//   make -C plugin SSHUD_STAGE=N
//   0=只验证加载  1=+共享内存  2=+BuildID  3=+找基址  4=完整(默认)
// 定位到崩溃阶段后记得改回来再正常构建。
// ---------------------------------------------------------------------------
#ifndef SSHUD_STAGE
#define SSHUD_STAGE 4
#endif

extern "C" int main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;
#if SSHUD_STAGE < 1
  SaltySD_printf("sshud: stage0 加载验证\n");
  return 0;
#endif

  if (!setupSharedMemory()) {
    SaltySD_printf("sshud: 共享内存申请失败\n");
    return 0;  // 必须 return，不能 exit
  }
  g_tracker.init(g_shared, readTickFreq(), 0);
  g_tracker.setHookStatus(SSHUD_HOOK_INIT_OK);
#if SSHUD_STAGE < 2
  SaltySD_printf("sshud: stage1 共享内存 OK\n");
  return 0;
#endif

  if (!verifyBuildId()) {
    SaltySD_printf("sshud: Build ID 不匹配，期望 %s。不做任何 patch。\n", SS_BUILD_ID_STR);
    return 0;
  }
  g_tracker.setHookStatus(SSHUD_HOOK_BUILDID_OK);
#if SSHUD_STAGE < 3
  SaltySD_printf("sshud: stage2 BuildID OK\n");
  return 0;
#endif

  const uint64_t mainBase = findMainNsoBase();
  if (mainBase == 0) {
    SaltySD_printf("sshud: 没能定位 main NSO 基址\n");
    return 0;
  }
  SaltySD_printf("sshud: main NSO @ %lx\n", mainBase);
#if SSHUD_STAGE < 4
  SaltySD_printf("sshud: stage3 基址定位 OK\n");
  return 0;
#endif

  if (installHook(mainBase + HM_TAKEDAMAGE, 0, reinterpret_cast<void*>(&takeDamageHook),
                  &g_origTakeDamage)) {
    g_tracker.setHookStatus(SSHUD_HOOK_TAKEDAMAGE);
    SaltySD_printf("sshud: TakeDamage hook 已安装\n");
  }

  g_tracker.publish(readTick());
  return 0;  // 返回给 elf_trampoline，游戏继续启动
}
