// 宿主机侧单元测试：tracker 的槽位管理、TTL 淘汰、seqlock 一致性。
// 构建：make -C tests && ./tests/test_tracker

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../plugin/source/tracker.hpp"

static int gFailures = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
      ++gFailures;                                                      \
    }                                                                   \
  } while (0)

#define CHECK_EQ(a, b)                                                       \
  do {                                                                       \
    auto va_ = (a); auto vb_ = (b);                                           \
    if (!(va_ == vb_)) {                                                      \
      std::printf("  FAIL %s:%d  %s == %s  (%lld vs %lld)\n", __FILE__,       \
                  __LINE__, #a, #b, (long long)va_, (long long)vb_);          \
      ++gFailures;                                                            \
    }                                                                         \
  } while (0)

static const uint64_t FREQ = 19200000;  // Switch CNTFRQ_EL0

static void testBasicTracking() {
  std::printf("test: 基本追踪与发布\n");
  SsHudBlock block;
  sshud::EntityTracker t;
  t.init(&block, FREQ, 0xFC9EA4CCC955D579ull);

  CHECK_EQ(block.magic, SSHUD_MAGIC);
  CHECK_EQ(block.version, SSHUD_VERSION);

  t.onDamage(0x1000, 40, 50, 10, 0xAABBCCDD, FREQ * 1);
  t.publish(FREQ * 1);

  CHECK_EQ(block.entityCount, 1u);
  CHECK_EQ(block.entities[0].objectId, 0x1000ull);
  CHECK_EQ(block.entities[0].hp, 40);
  CHECK_EQ(block.entities[0].maxHp, 50);
  CHECK_EQ(block.entities[0].lastDamage, 10);
  CHECK(block.entities[0].flags & SSHUD_FLAG_ALIVE);
  CHECK(!(block.entities[0].flags & SSHUD_FLAG_BOSS));
  CHECK_EQ(block.seq % 2, 0ull);  // 发布结束后必须是偶数
}

static void testMaxHpInference() {
  std::printf("test: maxHp 缺失时的推断\n");
  SsHudBlock block;
  sshud::EntityTracker t;
  t.init(&block, FREQ, 0);

  // maxHp 传 0（字段读不到），应回退为 hp + damage
  t.onDamage(0x2000, 30, 0, 12, 0, FREQ);
  t.publish(FREQ);
  CHECK_EQ(block.entities[0].maxHp, 42);

  // 后续拿到真实 maxHp 应覆盖推断值
  t.onDamage(0x2000, 20, 60, 10, 0, FREQ);
  t.publish(FREQ);
  CHECK_EQ(block.entities[0].maxHp, 60);
}

static void testBossFlag() {
  std::printf("test: Boss 阈值\n");
  SsHudBlock block;
  sshud::EntityTracker t;
  t.init(&block, FREQ, 0);

  t.onDamage(0x3000, 400, 500, 20, 0, FREQ);
  t.publish(FREQ);
  CHECK(block.entities[0].flags & SSHUD_FLAG_BOSS);

  t.onDamage(0x3001, 10, 20, 5, 0, FREQ);
  t.publish(FREQ);
  for (uint32_t i = 0; i < block.entityCount; ++i)
    if (block.entities[i].objectId == 0x3001)
      CHECK(!(block.entities[i].flags & SSHUD_FLAG_BOSS));
}

static void testDeath() {
  std::printf("test: 死亡清除 ALIVE 标志\n");
  SsHudBlock block;
  sshud::EntityTracker t;
  t.init(&block, FREQ, 0);

  t.onDamage(0x4000, 5, 50, 10, 0, FREQ);
  t.onDamage(0x4000, 0, 50, 5, 0, FREQ);
  t.publish(FREQ);
  CHECK(!(block.entities[0].flags & SSHUD_FLAG_ALIVE));

  t.onDamage(0x4001, 10, 50, 10, 0, FREQ);
  t.onDeath(0x4001);
  t.publish(FREQ);
  for (uint32_t i = 0; i < block.entityCount; ++i)
    if (block.entities[i].objectId == 0x4001)
      CHECK(!(block.entities[i].flags & SSHUD_FLAG_ALIVE));
}

static void testTtlEviction() {
  std::printf("test: TTL 超时淘汰\n");
  SsHudBlock block;
  sshud::EntityTracker t;
  t.init(&block, FREQ, 0);

  const uint64_t t0 = FREQ * 100;
  t.onDamage(0x5000, 40, 50, 10, 0, t0);
  t.publish(t0);
  CHECK_EQ(block.entityCount, 1u);

  // TTL 之内仍在
  t.publish(t0 + (uint64_t)(FREQ * (sshud::ENTITY_TTL_SECONDS - 0.5)));
  CHECK_EQ(block.entityCount, 1u);

  // 超过 TTL 应消失
  t.publish(t0 + (uint64_t)(FREQ * (sshud::ENTITY_TTL_SECONDS + 1.0)));
  CHECK_EQ(block.entityCount, 0u);
  CHECK_EQ(t.liveSlotCount(), 0u);
}

static void testSlotExhaustion() {
  std::printf("test: 槽位耗尽时淘汰最旧\n");
  SsHudBlock block;
  sshud::EntityTracker t;
  t.init(&block, FREQ, 0);

  // 填满槽位，lastHitTick 以毫秒递增 —— 必须全部落在 TTL 之内，
  // 否则测的就是 TTL 淘汰而不是 LRU 淘汰了。
  const uint64_t MS = FREQ / 1000;
  const uint64_t base = FREQ * 100;
  for (uint32_t i = 0; i < SSHUD_MAX_ENTITIES; ++i)
    t.onDamage(0x6000 + i, 10, 50, 1, 0, base + MS * i);
  CHECK_EQ(t.liveSlotCount(), (uint32_t)SSHUD_MAX_ENTITIES);

  // 再来一个新敌人：应淘汰 lastHitTick 最小的 0x6000
  const uint64_t tNew = base + MS * SSHUD_MAX_ENTITIES;
  t.onDamage(0x9999, 10, 50, 1, 0, tNew);
  t.publish(tNew);

  bool found9999 = false, found6000 = false;
  for (uint32_t i = 0; i < block.entityCount; ++i) {
    if (block.entities[i].objectId == 0x9999) found9999 = true;
    if (block.entities[i].objectId == 0x6000) found6000 = true;
  }
  CHECK(found9999);
  CHECK(!found6000);
  CHECK_EQ(t.liveSlotCount(), (uint32_t)SSHUD_MAX_ENTITIES);
}

static void testSeqlockParity() {
  std::printf("test: seqlock 奇偶性\n");
  SsHudBlock block;
  sshud::EntityTracker t;
  t.init(&block, FREQ, 0);

  uint64_t prev = block.seq;
  for (int i = 0; i < 10; ++i) {
    t.onDamage(0x7000, 50 - i, 50, 1, 0, FREQ * (1 + i));
    t.publish(FREQ * (1 + i));
    CHECK_EQ(block.seq % 2, 0ull);   // 发布完必须稳定
    CHECK(block.seq > prev);          // 序号必须单调递增
    prev = block.seq;
  }
}

static void testZeroObjectIdIgnored() {
  std::printf("test: objectId==0 被忽略\n");
  SsHudBlock block;
  sshud::EntityTracker t;
  t.init(&block, FREQ, 0);
  t.onDamage(0, 10, 50, 5, 0, FREQ);
  t.publish(FREQ);
  CHECK_EQ(block.entityCount, 0u);
}

static void testNullBlockSafe() {
  std::printf("test: 共享内存未映射时不崩\n");
  sshud::EntityTracker t;
  t.init(nullptr, FREQ, 0);
  t.onDamage(0x8000, 10, 50, 5, 0, FREQ);
  t.onDeath(0x8000);
  t.publish(FREQ);
  t.setHookStatus(SSHUD_HOOK_INIT_OK);
  CHECK_EQ(t.liveSlotCount(), 1u);
}

static void testNameHash() {
  std::printf("test: FNV-1a 哈希稳定性\n");
  CHECK_EQ(sshudHashName("Hornet"), sshudHashName("Hornet"));
  CHECK(sshudHashName("Hornet") != sshudHashName("hornet"));
  CHECK_EQ(sshudHashName(nullptr), 0u);
  CHECK_EQ(sshudHashName(""), 2166136261u);
}

int main() {
  std::printf("=== sshud tracker 单元测试 ===\n");
  testBasicTracking();
  testMaxHpInference();
  testBossFlag();
  testDeath();
  testTtlEviction();
  testSlotExhaustion();
  testSeqlockParity();
  testZeroObjectIdIgnored();
  testNullBlockSafe();
  testNameHash();

  if (gFailures) {
    std::printf("\n%d 项失败\n", gFailures);
    return 1;
  }
  std::printf("\n全部通过\n");
  return 0;
}
