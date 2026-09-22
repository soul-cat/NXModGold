// ARM64 指令重定位 —— 内联 hook 的核心难点。
//
// 把函数开头几条指令搬到别处（trampoline）时，PC 相对指令的立即数必须重算，
// 否则跳转目标会错到天上去。本文件只做判定与重写，不做任何内存写入，
// 因此可以在宿主机上完整单元测试。
//
// 设计立场：**宁可拒绝，也不要写出半对的代码。**
// 遇到搬不动的指令就报 Unrelocatable，让调用方放弃安装 hook 并点亮诊断位，
// 而不是"尽力而为"地生成一个会让游戏随机崩溃的 trampoline。
//
// 对函数序言（prologue）来说这个策略成本极低：典型的
// `stp x29, x30, [sp, #-0x20]!` / `mov x29, sp` / `sub sp, sp, #N`
// 全都不是 PC 相对指令，直接原样搬走即可。

#ifndef SS_ARM64_RELOC_HPP
#define SS_ARM64_RELOC_HPP

#include <stdint.h>

namespace sshud {
namespace arm64 {

/// 单条指令的重定位判定结果。
enum class RelocVerdict {
  /// 与 PC 无关，原样复制即可。
  Copyable,
  /// PC 相对，但可以改写立即数搬到新位置。
  Rewritable,
  /// 搬不动 —— 立即数范围太小（B.cond / CBZ / TBZ / LDR literal），
  /// 搬到插件 .text 后位移必然溢出。必须放弃 hook。
  Unrelocatable,
};

// --- 指令编码掩码（ARMv8-A A64）---
constexpr uint32_t MASK_BRANCH_IMM = 0xFC000000u;  // B / BL
constexpr uint32_t OP_B = 0x14000000u;
constexpr uint32_t OP_BL = 0x94000000u;

constexpr uint32_t MASK_BCOND = 0xFF000010u;  // B.cond
constexpr uint32_t OP_BCOND = 0x54000000u;

constexpr uint32_t MASK_COMPARE_BRANCH = 0x7F000000u;  // CBZ / CBNZ
constexpr uint32_t OP_CBZ = 0x34000000u;
constexpr uint32_t OP_CBNZ = 0x35000000u;

constexpr uint32_t MASK_TEST_BRANCH = 0x7F000000u;  // TBZ / TBNZ
constexpr uint32_t OP_TBZ = 0x36000000u;
constexpr uint32_t OP_TBNZ = 0x37000000u;

constexpr uint32_t MASK_ADR = 0x9F000000u;  // ADR / ADRP
constexpr uint32_t OP_ADR = 0x10000000u;
constexpr uint32_t OP_ADRP = 0x90000000u;

constexpr uint32_t MASK_LDR_LITERAL = 0x3B000000u;
constexpr uint32_t OP_LDR_LITERAL = 0x18000000u;

/// B/BL 的位移范围：imm26 << 2，带符号 ±128 MiB。
constexpr int64_t BRANCH_MIN = -((int64_t)1 << 27);
constexpr int64_t BRANCH_MAX = ((int64_t)1 << 27) - 4;

/// ADR 的位移范围：imm21，带符号 ±1 MiB。
constexpr int64_t ADR_MIN = -((int64_t)1 << 20);
constexpr int64_t ADR_MAX = ((int64_t)1 << 20) - 1;

/// ADRP 的页位移范围：imm21 << 12，带符号 ±4 GiB。
constexpr int64_t ADRP_MIN = -((int64_t)1 << 32);
constexpr int64_t ADRP_MAX = ((int64_t)1 << 32) - 4096;

/// 把 width 位的值符号扩展成 int64。
inline int64_t signExtend(uint64_t value, int width) {
  const uint64_t signBit = 1ull << (width - 1);
  if (value & signBit) {
    return (int64_t)(value | ~((1ull << width) - 1));
  }
  return (int64_t)value;
}

/// 判定一条指令能否被搬到别处。
inline RelocVerdict classify(uint32_t insn) {
  if ((insn & MASK_BRANCH_IMM) == OP_B || (insn & MASK_BRANCH_IMM) == OP_BL) {
    return RelocVerdict::Rewritable;
  }
  if ((insn & MASK_ADR) == OP_ADR || (insn & MASK_ADR) == OP_ADRP) {
    return RelocVerdict::Rewritable;
  }
  // 以下几类立即数范围只有 ±1MiB 甚至 ±32KiB，
  // 搬到插件 .text 后（与游戏 .text 相距可能上 GiB）必然溢出，直接拒绝。
  if ((insn & MASK_BCOND) == OP_BCOND) return RelocVerdict::Unrelocatable;
  if ((insn & MASK_COMPARE_BRANCH) == OP_CBZ ||
      (insn & MASK_COMPARE_BRANCH) == OP_CBNZ) {
    return RelocVerdict::Unrelocatable;
  }
  if ((insn & MASK_TEST_BRANCH) == OP_TBZ ||
      (insn & MASK_TEST_BRANCH) == OP_TBNZ) {
    return RelocVerdict::Unrelocatable;
  }
  if ((insn & MASK_LDR_LITERAL) == OP_LDR_LITERAL) return RelocVerdict::Unrelocatable;
  return RelocVerdict::Copyable;
}

/// 把 `insn`（原本位于 from）重写成可以放在 to 处执行的形式。
///
/// 返回 false 表示搬不动 —— 调用方必须放弃安装 hook。
/// 成功时结果写入 *out；Copyable 的指令原样透传。
inline bool relocate(uint32_t insn, uint64_t from, uint64_t to, uint32_t* out) {
  if (out == nullptr) return false;

  switch (classify(insn)) {
    case RelocVerdict::Unrelocatable:
      return false;

    case RelocVerdict::Copyable:
      *out = insn;
      return true;

    case RelocVerdict::Rewritable:
      break;
  }

  // --- B / BL ---
  if ((insn & MASK_BRANCH_IMM) == OP_B || (insn & MASK_BRANCH_IMM) == OP_BL) {
    const int64_t oldOffset = signExtend(insn & 0x03FFFFFFu, 26) * 4;
    const uint64_t target = from + (uint64_t)oldOffset;
    const int64_t newOffset = (int64_t)target - (int64_t)to;
    if (newOffset < BRANCH_MIN || newOffset > BRANCH_MAX || (newOffset & 3) != 0) {
      return false;
    }
    const uint32_t imm26 = (uint32_t)((newOffset / 4) & 0x03FFFFFF);
    *out = (insn & MASK_BRANCH_IMM) | imm26;
    return true;
  }

  // --- ADR ---
  if ((insn & MASK_ADR) == OP_ADR) {
    const uint32_t immlo = (insn >> 29) & 0x3u;
    const uint32_t immhi = (insn >> 5) & 0x7FFFFu;
    const int64_t oldOffset = signExtend(((uint64_t)immhi << 2) | immlo, 21);
    const uint64_t target = from + (uint64_t)oldOffset;
    const int64_t newOffset = (int64_t)target - (int64_t)to;
    if (newOffset < ADR_MIN || newOffset > ADR_MAX) return false;
    const uint32_t imm21 = (uint32_t)(newOffset & 0x1FFFFF);
    *out = (insn & ~((0x3u << 29) | (0x7FFFFu << 5))) | ((imm21 & 0x3u) << 29) |
           (((imm21 >> 2) & 0x7FFFFu) << 5);
    return true;
  }

  // --- ADRP（页对齐，立即数单位是 4KiB 页）---
  if ((insn & MASK_ADR) == OP_ADRP) {
    const uint32_t immlo = (insn >> 29) & 0x3u;
    const uint32_t immhi = (insn >> 5) & 0x7FFFFu;
    const int64_t oldPageOffset = signExtend(((uint64_t)immhi << 2) | immlo, 21) * 4096;
    const uint64_t fromPage = from & ~0xFFFull;
    const uint64_t targetPage = fromPage + (uint64_t)oldPageOffset;
    const uint64_t toPage = to & ~0xFFFull;
    const int64_t newPageOffset = (int64_t)targetPage - (int64_t)toPage;
    if (newPageOffset < ADRP_MIN || newPageOffset > ADRP_MAX) return false;
    const uint32_t imm21 = (uint32_t)((newPageOffset / 4096) & 0x1FFFFF);
    *out = (insn & ~((0x3u << 29) | (0x7FFFFu << 5))) | ((imm21 & 0x3u) << 29) |
           (((imm21 >> 2) & 0x7FFFFu) << 5);
    return true;
  }

  return false;
}

/// 生成一条绝对跳转序列：LDR X17, #8 ; BR X17 ; <8 字节目标地址>
///
/// 用 X17 是因为它是 AAPCS64 的 IP1（过程间调用临时寄存器），
/// 在函数入口处可以自由破坏。SaltyNX Core 自己用的是 X4，
/// 但 X4 可能承载实参，在函数序言处并不安全。
///
/// 写入 out[0..3]（16 字节）。
constexpr uint32_t INSN_LDR_X17_PC8 = 0x58000051u;  // ldr x17, #8
constexpr uint32_t INSN_BR_X17 = 0xD61F0220u;       // br  x17
constexpr int ABS_JUMP_WORDS = 4;                   // 16 字节

inline void makeAbsoluteJump(uint64_t target, uint32_t* out) {
  out[0] = INSN_LDR_X17_PC8;
  out[1] = INSN_BR_X17;
  out[2] = (uint32_t)(target & 0xFFFFFFFFull);
  out[3] = (uint32_t)(target >> 32);
}

/// 安装一个 hook 需要搬走的指令条数。绝对跳转占 16 字节 = 4 条指令。
constexpr int STOLEN_INSN_COUNT = ABS_JUMP_WORDS;

/// trampoline 需要的字数：搬走的指令 + 跳回原函数的绝对跳转。
constexpr int TRAMPOLINE_WORDS = STOLEN_INSN_COUNT + ABS_JUMP_WORDS;

/// 构建 trampoline 内容。
///
/// @param original   原函数开头 STOLEN_INSN_COUNT 条指令
/// @param fromAddr   原函数地址（运行时绝对地址）
/// @param trampAddr  trampoline 将要驻留的地址（运行时绝对地址）
/// @param out        输出缓冲，至少 TRAMPOLINE_WORDS 个 uint32_t
/// @return           false 表示有指令搬不动，必须放弃安装 hook
inline bool buildTrampoline(const uint32_t* original, uint64_t fromAddr,
                            uint64_t trampAddr, uint32_t* out) {
  if (original == nullptr || out == nullptr) return false;

  for (int i = 0; i < STOLEN_INSN_COUNT; ++i) {
    const uint64_t insnFrom = fromAddr + (uint64_t)(i * 4);
    const uint64_t insnTo = trampAddr + (uint64_t)(i * 4);
    if (!relocate(original[i], insnFrom, insnTo, &out[i])) {
      return false;
    }
  }
  // 搬完之后跳回原函数被覆盖区域的下一条指令
  makeAbsoluteJump(fromAddr + (uint64_t)(STOLEN_INSN_COUNT * 4),
                   &out[STOLEN_INSN_COUNT]);
  return true;
}

}  // namespace arm64
}  // namespace sshud

#endif  // SS_ARM64_RELOC_HPP
