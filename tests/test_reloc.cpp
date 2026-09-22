// ARM64 指令重定位测试。
//
// 这是整个项目风险最高的代码：立即数字段写错一位，游戏就会跳到随机地址崩掉。
//
// 测试架构分两层：
//   1. 下方的参考编码全部来自 macOS 系统汇编器（cc -arch arm64）的真实输出，
//      不是手算的。用它们验证解码器。
//   2. 用验证过的解码器做性质测试：重定位后解析出的目标地址必须与重定位前完全一致。
//      这比比对固定编码强得多——它覆盖了任意 (from, to) 组合。

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <random>

#include "../plugin/source/arm64_reloc.hpp"

using namespace sshud::arm64;

static int gFailures = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    if (!(cond)) {                                                  \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
      ++gFailures;                                                  \
    }                                                               \
  } while (0)

#define CHECK_EQ_HEX(a, b)                                                       \
  do {                                                                            \
    uint64_t va_ = (uint64_t)(a), vb_ = (uint64_t)(b);                            \
    if (va_ != vb_) {                                                             \
      std::printf("  FAIL %s:%d  %s == %s  (0x%llX vs 0x%llX)\n", __FILE__,       \
                  __LINE__, #a, #b, (unsigned long long)va_,                      \
                  (unsigned long long)vb_);                                       \
      ++gFailures;                                                                \
    }                                                                             \
  } while (0)

// ---------------------------------------------------------------------------
// 参考编码 —— 由 macOS 系统汇编器生成，见 tests/gen_ground_truth.sh
// ---------------------------------------------------------------------------
namespace ref {
constexpr uint32_t B_PLUS_0x1000     = 0x14000400u;  // b    .+0x1000
constexpr uint32_t BL_MINUS_0x100    = 0x97FFFFC0u;  // bl   .-0x100
constexpr uint32_t ADR_X0_PLUS_0x100 = 0x10000800u;  // adr  x0, .+0x100
constexpr uint32_t ADR_X5_MINUS_0x200= 0x10FFF005u;  // adr  x5, .-0x200
constexpr uint32_t LDR_X17_PLUS_8    = 0x58000051u;  // ldr  x17, .+8
constexpr uint32_t BR_X17            = 0xD61F0220u;  // br   x17
constexpr uint32_t B_EQ_PLUS_0x40    = 0x54000200u;  // b.eq .+0x40
constexpr uint32_t CBZ_X0_PLUS_0x40  = 0xB4000200u;  // cbz  x0, .+0x40
constexpr uint32_t CBNZ_X1_MINUS_0x40= 0xB5FFFE01u;  // cbnz x1, .-0x40
constexpr uint32_t TBZ_W0_PLUS_0x40  = 0x36180200u;  // tbz  w0, #3, .+0x40
constexpr uint32_t TBNZ_X2_MINUS_0x40= 0xB747FE02u;  // tbnz x2, #40, .-0x40
constexpr uint32_t LDR_X0_PLUS_0x40  = 0x58000200u;  // ldr  x0, .+0x40
constexpr uint32_t STP_PROLOGUE      = 0xA9BE7BFDu;  // stp  x29, x30, [sp, #-0x20]!
constexpr uint32_t MOV_X29_SP        = 0x910003FDu;  // mov  x29, sp
constexpr uint32_t SUB_SP_0x40       = 0xD10103FFu;  // sub  sp, sp, #0x40
constexpr uint32_t RET               = 0xD65F03C0u;  // ret
}  // namespace ref

// ---------------------------------------------------------------------------
// 解码器 —— 独立于被测代码实现，用来交叉验证
// ---------------------------------------------------------------------------
static uint64_t decodeBranch(uint32_t insn, uint64_t pc) {
  return pc + (uint64_t)(signExtend(insn & 0x03FFFFFFu, 26) * 4);
}

static uint64_t decodeAdr(uint32_t insn, uint64_t pc) {
  const uint32_t immlo = (insn >> 29) & 0x3u;
  const uint32_t immhi = (insn >> 5) & 0x7FFFFu;
  return pc + (uint64_t)signExtend(((uint64_t)immhi << 2) | immlo, 21);
}

static uint64_t decodeAdrp(uint32_t insn, uint64_t pc) {
  const uint32_t immlo = (insn >> 29) & 0x3u;
  const uint32_t immhi = (insn >> 5) & 0x7FFFFu;
  return (pc & ~0xFFFull) +
         (uint64_t)(signExtend(((uint64_t)immhi << 2) | immlo, 21) * 4096);
}

/// 用参考编码构造一条 ADRP（汇编器不接受字面量偏移的 adrp，只能这样造）
static uint32_t encodeAdrp(int rd, int64_t pageDelta) {
  const uint32_t imm21 = (uint32_t)((pageDelta / 4096) & 0x1FFFFF);
  return OP_ADRP | ((imm21 & 0x3u) << 29) | (((imm21 >> 2) & 0x7FFFFu) << 5) |
         (uint32_t)(rd & 0x1F);
}

// ---------------------------------------------------------------------------
static void testDecoderAgainstAssembler() {
  std::printf("test: 解码器 vs 系统汇编器参考编码\n");
  // 汇编器输出的每条指令都在各自地址上；这里用相对形式验证
  CHECK_EQ_HEX(decodeBranch(ref::B_PLUS_0x1000, 0), 0x1000u);
  CHECK_EQ_HEX(decodeBranch(ref::BL_MINUS_0x100, 0x4), 0x4 - 0x100);
  CHECK_EQ_HEX(decodeAdr(ref::ADR_X0_PLUS_0x100, 0x8), 0x108u);
  CHECK_EQ_HEX(decodeAdr(ref::ADR_X5_MINUS_0x200, 0xC), 0xC - 0x200);

  // 自造的 ADRP 能被自己的解码器还原
  CHECK_EQ_HEX(decodeAdrp(encodeAdrp(0, 0x1000), 0x0), 0x1000u);
  CHECK_EQ_HEX(decodeAdrp(encodeAdrp(0, -0x2000), 0x5000), 0x3000u);
}

static void testClassification() {
  std::printf("test: 指令分类\n");
  // 可重写
  CHECK(classify(ref::B_PLUS_0x1000) == RelocVerdict::Rewritable);
  CHECK(classify(ref::BL_MINUS_0x100) == RelocVerdict::Rewritable);
  CHECK(classify(ref::ADR_X0_PLUS_0x100) == RelocVerdict::Rewritable);
  CHECK(classify(encodeAdrp(0, 0x1000)) == RelocVerdict::Rewritable);

  // 必须拒绝 —— 立即数范围太小
  CHECK(classify(ref::B_EQ_PLUS_0x40) == RelocVerdict::Unrelocatable);
  CHECK(classify(ref::CBZ_X0_PLUS_0x40) == RelocVerdict::Unrelocatable);
  CHECK(classify(ref::CBNZ_X1_MINUS_0x40) == RelocVerdict::Unrelocatable);
  CHECK(classify(ref::TBZ_W0_PLUS_0x40) == RelocVerdict::Unrelocatable);
  CHECK(classify(ref::TBNZ_X2_MINUS_0x40) == RelocVerdict::Unrelocatable);
  CHECK(classify(ref::LDR_X0_PLUS_0x40) == RelocVerdict::Unrelocatable);
  CHECK(classify(ref::LDR_X17_PLUS_8) == RelocVerdict::Unrelocatable);

  // 与 PC 无关，原样搬 —— 典型函数序言
  CHECK(classify(ref::STP_PROLOGUE) == RelocVerdict::Copyable);
  CHECK(classify(ref::MOV_X29_SP) == RelocVerdict::Copyable);
  CHECK(classify(ref::SUB_SP_0x40) == RelocVerdict::Copyable);
  CHECK(classify(ref::RET) == RelocVerdict::Copyable);
  CHECK(classify(ref::BR_X17) == RelocVerdict::Copyable);
}

static void testCopyablePassThrough() {
  std::printf("test: 无关指令原样透传\n");
  const uint32_t insns[] = {ref::STP_PROLOGUE, ref::MOV_X29_SP, ref::SUB_SP_0x40,
                            ref::RET, ref::BR_X17};
  for (uint32_t insn : insns) {
    uint32_t out = 0;
    CHECK(relocate(insn, 0x71000000ull, 0x80000000ull, &out));
    CHECK_EQ_HEX(out, insn);
  }
}

/// 核心性质：重定位后解析出的目标地址必须与重定位前完全一致。
static void testRoundTripProperty() {
  std::printf("test: 性质测试 —— 重定位不改变目标地址\n");
  std::mt19937_64 rng(0xC0FFEE);
  std::uniform_int_distribution<uint64_t> addrDist(0, 0x3FFFFFFFull);

  int branchChecked = 0, adrChecked = 0, adrpChecked = 0, refused = 0;

  for (int iter = 0; iter < 20000; ++iter) {
    // 4 字节对齐的随机源/目标地址
    const uint64_t from = (addrDist(rng) & ~3ull) + 0x71000000ull;
    const uint64_t to = (addrDist(rng) & ~3ull) + 0x71000000ull;

    // --- B ---
    {
      const int64_t disp = ((int64_t)(rng() % 0x2000000) - 0x1000000) * 4;
      const uint32_t insn = OP_B | (uint32_t)((disp / 4) & 0x03FFFFFF);
      const uint64_t origTarget = decodeBranch(insn, from);
      uint32_t out = 0;
      if (relocate(insn, from, to, &out)) {
        CHECK_EQ_HEX(decodeBranch(out, to), origTarget);
        CHECK_EQ_HEX(out & MASK_BRANCH_IMM, OP_B);  // opcode 不能被改掉
        ++branchChecked;
      } else {
        ++refused;
      }
    }

    // --- ADR ---
    // ADR 的范围只有 ±1MiB，而插件 .text 与游戏 .text 往往相距上 GiB，
    // 所以这里必须用「近距离」的 to 才能测到重写路径本身。
    // 远距离必被拒绝，那是正确行为，单独在下面统计。
    {
      const int64_t disp = (int64_t)(rng() % 0x200000) - 0x100000;
      const uint32_t imm21 = (uint32_t)(disp & 0x1FFFFF);
      const uint32_t insn =
          OP_ADR | ((imm21 & 0x3u) << 29) | (((imm21 >> 2) & 0x7FFFFu) << 5) | 7u;
      const uint64_t origTarget = decodeAdr(insn, from);

      // 近距离：to 在 from 的 ±64KiB 内，重写应当成功
      const int64_t nearDelta = ((int64_t)(rng() % 0x8000) - 0x4000) * 4;
      const uint64_t nearTo = from + (uint64_t)nearDelta;
      uint32_t out = 0;
      if (relocate(insn, from, nearTo, &out)) {
        CHECK_EQ_HEX(decodeAdr(out, nearTo), origTarget);
        CHECK_EQ_HEX(out & 0x1Fu, 7u);              // Rd 不能被改掉
        CHECK_EQ_HEX(out & MASK_ADR, OP_ADR);
        ++adrChecked;
      }

      // 远距离：必须被拒绝，绝不能悄悄生成越界编码
      uint32_t farOut = 0;
      const uint64_t farTo = from + 0x40000000ull;
      if (relocate(insn, from, farTo, &farOut)) {
        // 万一真的成功了，那结果也必须是正确的——不允许静默错误
        CHECK_EQ_HEX(decodeAdr(farOut, farTo), origTarget);
      } else {
        ++refused;
      }
    }

    // --- ADRP（页对齐的 from/to）---
    {
      const int64_t pageDelta = ((int64_t)(rng() % 0x200000) - 0x100000) * 4096;
      const uint32_t insn = encodeAdrp(9, pageDelta);
      const uint64_t origTarget = decodeAdrp(insn, from);
      uint32_t out = 0;
      if (relocate(insn, from, to, &out)) {
        CHECK_EQ_HEX(decodeAdrp(out, to), origTarget);
        CHECK_EQ_HEX(out & 0x1Fu, 9u);
        CHECK_EQ_HEX(out & MASK_ADR, OP_ADRP);
        ++adrpChecked;
      } else {
        ++refused;
      }
    }
    if (gFailures > 0) break;  // 出错就停，避免刷屏
  }

  std::printf("    B: %d 条  ADR: %d 条  ADRP: %d 条  越界拒绝: %d 条\n",
              branchChecked, adrChecked, adrpChecked, refused);
  CHECK(branchChecked > 1000);
  CHECK(adrChecked > 10000);
  CHECK(adrpChecked > 1000);
  CHECK(refused > 0);  // 必须真的有越界被拒的样本，否则没测到边界
}

static void testOutOfRangeRefused() {
  std::printf("test: 超出立即数范围时拒绝\n");
  // B 的范围是 ±128MiB。搬到 1GiB 之外必然放不下。
  const uint32_t b = OP_B | 0x100u;  // b .+0x400
  uint32_t out = 0;
  CHECK(!relocate(b, 0x71000000ull, 0x71000000ull + 0x40000000ull, &out));

  // ADR 的范围是 ±1MiB
  const uint32_t adr = ref::ADR_X0_PLUS_0x100;
  CHECK(!relocate(adr, 0x71000000ull, 0x71000000ull + 0x200000ull, &out));

  // 同样的指令搬到近处应该成功
  CHECK(relocate(b, 0x71000000ull, 0x71000100ull, &out));
  CHECK(relocate(adr, 0x71000000ull, 0x71000100ull, &out));
}

static void testAbsoluteJump() {
  std::printf("test: 绝对跳转序列编码\n");
  uint32_t buf[ABS_JUMP_WORDS] = {};
  const uint64_t target = 0x7123456789ABCDEFull;
  makeAbsoluteJump(target, buf);

  // 前两条必须与系统汇编器的输出一致
  CHECK_EQ_HEX(buf[0], ref::LDR_X17_PLUS_8);
  CHECK_EQ_HEX(buf[1], ref::BR_X17);
  // 后 8 字节是小端的目标地址
  uint64_t stored = 0;
  std::memcpy(&stored, &buf[2], sizeof(stored));
  CHECK_EQ_HEX(stored, target);
}

static void testBuildTrampoline() {
  std::printf("test: trampoline 构建\n");
  const uint64_t fn = 0x7100100000ull;     // 原函数
  const uint64_t tramp = 0x7200200000ull;  // trampoline 驻地

  // 典型序言：全是与 PC 无关的指令
  const uint32_t prologue[STOLEN_INSN_COUNT] = {
      ref::STP_PROLOGUE, ref::MOV_X29_SP, ref::SUB_SP_0x40, ref::MOV_X29_SP};

  uint32_t out[TRAMPOLINE_WORDS] = {};
  CHECK(buildTrampoline(prologue, fn, tramp, out));

  // 前 4 条原样搬过来
  for (int i = 0; i < STOLEN_INSN_COUNT; ++i) CHECK_EQ_HEX(out[i], prologue[i]);
  // 后面跟着跳回 fn + 16 的绝对跳转
  CHECK_EQ_HEX(out[STOLEN_INSN_COUNT], ref::LDR_X17_PLUS_8);
  CHECK_EQ_HEX(out[STOLEN_INSN_COUNT + 1], ref::BR_X17);
  uint64_t back = 0;
  std::memcpy(&back, &out[STOLEN_INSN_COUNT + 2], sizeof(back));
  CHECK_EQ_HEX(back, fn + STOLEN_INSN_COUNT * 4);
}

static void testBuildTrampolineRefusesUnrelocatable() {
  std::printf("test: 序言含不可搬指令时拒绝构建\n");
  const uint64_t fn = 0x7100100000ull;
  const uint64_t tramp = 0x7200200000ull;
  uint32_t out[TRAMPOLINE_WORDS] = {};

  // 每个位置都放一次不可搬指令，都必须被拒
  for (int pos = 0; pos < STOLEN_INSN_COUNT; ++pos) {
    uint32_t prologue[STOLEN_INSN_COUNT] = {ref::STP_PROLOGUE, ref::MOV_X29_SP,
                                            ref::SUB_SP_0x40, ref::MOV_X29_SP};
    prologue[pos] = ref::CBZ_X0_PLUS_0x40;
    CHECK(!buildTrampoline(prologue, fn, tramp, out));
  }

  // 距离过远导致 B 放不下时也必须拒
  uint32_t farPrologue[STOLEN_INSN_COUNT] = {OP_B | 0x100u, ref::MOV_X29_SP,
                                             ref::SUB_SP_0x40, ref::RET};
  CHECK(!buildTrampoline(farPrologue, fn, fn + 0x40000000ull, out));
}

static void testNullSafety() {
  std::printf("test: 空指针不崩\n");
  uint32_t out = 0;
  CHECK(!relocate(ref::B_PLUS_0x1000, 0, 0, nullptr));
  CHECK(!buildTrampoline(nullptr, 0, 0, &out));
  uint32_t buf[TRAMPOLINE_WORDS] = {};
  const uint32_t prologue[STOLEN_INSN_COUNT] = {ref::RET, ref::RET, ref::RET, ref::RET};
  CHECK(!buildTrampoline(prologue, 0, 0, nullptr));
  CHECK(buildTrampoline(prologue, 0x1000, 0x2000, buf));  // 正常路径仍然可用
}

int main() {
  std::printf("=== ARM64 重定位测试 ===\n");
  testDecoderAgainstAssembler();
  testClassification();
  testCopyablePassThrough();
  testOutOfRangeRefused();
  testAbsoluteJump();
  testBuildTrampoline();
  testBuildTrampolineRefusesUnrelocatable();
  testNullSafety();
  testRoundTripProperty();

  if (gFailures) {
    std::printf("\n%d 项失败\n", gFailures);
    return 1;
  }
  std::printf("\n全部通过\n");
  return 0;
}
