// 并发测试：tracker（写者，模拟游戏主线程）与 HudReader（读者，模拟 overlay）
// 真线程对跑，验证 seqlock 永不返回撕裂快照。
//
// 一致性判据：任何一份成功返回的快照里，每个 entity 的 hp/maxHp/lastDamage
// 必须满足写者的不变量。写者按固定规律生成数据，读者据此反查。

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "../plugin/source/tracker.hpp"
#include "../overlay/source/hud_reader.hpp"

static int gFailures = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    if (!(cond)) {                                                  \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
      ++gFailures;                                                  \
    }                                                               \
  } while (0)

static const uint64_t FREQ = 19200000;

// 写者的不变量：对 objectId = 0x1000 + k 的敌人，
//   maxHp == 100 + k
//   hp    == maxHp - lastDamage
// 读者在任何一致快照里都必须看到这个关系成立。
static bool entityInvariantHolds(const SsHudEntity& e) {
  if (e.objectId < 0x1000 || e.objectId >= 0x1000 + SSHUD_MAX_ENTITIES) return false;
  const int32_t k = (int32_t)(e.objectId - 0x1000);
  if (e.maxHp != 100 + k) return false;
  if (e.hp != e.maxHp - e.lastDamage) return false;
  return true;
}

static void testConcurrentReadWrite() {
  std::printf("test: 写者/读者并发，快照不得撕裂\n");

  // 用 aligned storage 模拟共享内存段
  alignas(64) static SsHudBlock shared;
  memset(&shared, 0, sizeof(shared));

  sshud::EntityTracker tracker;
  tracker.init(&shared, FREQ, 0xFC9EA4CCC955D579ull);

  sshud::HudReader reader;
  reader.attach(&shared);

  std::atomic<bool> stop{false};
  std::atomic<long> writeCount{0};

  // 写者：不停地改所有槽位的数据，保持不变量
  std::thread writer([&] {
    uint64_t tick = FREQ * 100;
    int32_t dmg = 1;
    while (!stop.load(std::memory_order_relaxed)) {
      for (int32_t k = 0; k < (int32_t)SSHUD_MAX_ENTITIES; ++k) {
        const int32_t maxHp = 100 + k;
        const int32_t d = (dmg % maxHp);
        tracker.onDamage(0x1000 + k, maxHp - d, maxHp, d, 0, tick);
      }
      tracker.publish(tick);
      writeCount.fetch_add(1, std::memory_order_relaxed);
      ++dmg;
      tick += FREQ / 1000;  // 每次前进 1ms，远小于 TTL
    }
  });

  // 读者：不停地取快照并校验
  long okSnapshots = 0, tornSnapshots = 0, badEntities = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
  SsHudBlock snap;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto st = reader.snapshot(&snap);
    if (st == sshud::ReadStatus::Torn) { ++tornSnapshots; continue; }
    if (st != sshud::ReadStatus::Ok) {
      std::printf("  FAIL 非预期状态: %s\n", sshud::HudReader::statusText(st));
      ++gFailures;
      break;
    }
    ++okSnapshots;
    for (uint32_t i = 0; i < snap.entityCount; ++i) {
      if (!entityInvariantHolds(snap.entities[i])) {
        ++badEntities;
        if (badEntities <= 3) {
          std::printf("  FAIL 撕裂: id=0x%llX hp=%d maxHp=%d dmg=%d\n",
                      (unsigned long long)snap.entities[i].objectId,
                      snap.entities[i].hp, snap.entities[i].maxHp,
                      snap.entities[i].lastDamage);
        }
      }
    }
  }

  stop.store(true);
  writer.join();

  std::printf("    写入 %ld 次，读到一致快照 %ld 份，重试耗尽 %ld 份\n",
              writeCount.load(), okSnapshots, tornSnapshots);
  CHECK(okSnapshots > 100);       // 必须真的读到过东西，否则测试没意义
  CHECK(badEntities == 0);        // 核心断言：一次撕裂都不允许
  if (badEntities) {
    std::printf("  FAIL 共 %ld 个撕裂条目\n", badEntities);
    ++gFailures;
  }
}

static void testRejectsBadMagic() {
  std::printf("test: magic 不符时拒绝\n");
  SsHudBlock block;
  memset(&block, 0, sizeof(block));
  block.magic = 0xDEADBEEF;
  block.version = SSHUD_VERSION;

  sshud::HudReader r;
  r.attach(&block);
  SsHudBlock out;
  CHECK(r.snapshot(&out) == sshud::ReadStatus::BadMagic);
}

static void testRejectsVersionMismatch() {
  std::printf("test: 版本不符时拒绝\n");
  SsHudBlock block;
  memset(&block, 0, sizeof(block));
  block.magic = SSHUD_MAGIC;
  block.version = SSHUD_VERSION + 99;

  sshud::HudReader r;
  r.attach(&block);
  SsHudBlock out;
  CHECK(r.snapshot(&out) == sshud::ReadStatus::VersionMismatch);
}

static void testNotAttached() {
  std::printf("test: 未连接时返回 NotAttached\n");
  sshud::HudReader r;
  SsHudBlock out;
  CHECK(!r.isAttached());
  CHECK(r.snapshot(&out) == sshud::ReadStatus::NotAttached);
}

static void testClampsHostileEntityCount() {
  std::printf("test: entityCount 被恶意/损坏值污染时夹取\n");
  SsHudBlock block;
  memset(&block, 0, sizeof(block));
  block.magic = SSHUD_MAGIC;
  block.version = SSHUD_VERSION;
  block.seq = 2;
  block.entityCount = 0xFFFFFFFFu;  // 越界值

  sshud::HudReader r;
  r.attach(&block);
  SsHudBlock out;
  CHECK(r.snapshot(&out) == sshud::ReadStatus::Ok);
  CHECK(out.entityCount == SSHUD_MAX_ENTITIES);
}

static void testOddSeqIsTorn() {
  std::printf("test: seq 停在奇数（写者卡死）时重试耗尽\n");
  SsHudBlock block;
  memset(&block, 0, sizeof(block));
  block.magic = SSHUD_MAGIC;
  block.version = SSHUD_VERSION;
  block.seq = 3;  // 奇数，永远停在"写入中"

  sshud::HudReader r;
  r.attach(&block);
  SsHudBlock out;
  CHECK(r.snapshot(&out) == sshud::ReadStatus::Torn);
}

static void testHelpers() {
  std::printf("test: 坐标换算与血量比例\n");
  CHECK(sshud::normToScreenX(0.0f) == 0);
  CHECK(sshud::normToScreenX(1.0f) == sshud::TESLA_SCREEN_W);
  CHECK(sshud::normToScreenX(0.5f) == 640);
  CHECK(sshud::normToScreenY(0.5f) == 360);
  CHECK(sshud::normToScreenX(-5.0f) == 0);           // 越界夹取
  CHECK(sshud::normToScreenY(9.0f) == sshud::TESLA_SCREEN_H);

  SsHudEntity e{};
  e.hp = 25; e.maxHp = 100;
  CHECK(sshud::hpFraction(e) > 0.24f && sshud::hpFraction(e) < 0.26f);
  e.maxHp = 0;
  CHECK(sshud::hpFraction(e) == 0.0f);               // 除零防护
  e.maxHp = 100; e.hp = -50;
  CHECK(sshud::hpFraction(e) == 0.0f);               // 负血夹取
  e.hp = 500;
  CHECK(sshud::hpFraction(e) == 1.0f);               // 超额夹取
}

static void testHookDiagnosis() {
  std::printf("test: hookStatus 诊断文本\n");
  CHECK(sshud::HudReader::hookDiagnosis(0) != nullptr);
  CHECK(sshud::HudReader::hookDiagnosis(SSHUD_HOOK_INIT_OK) != nullptr);
  const uint32_t all = SSHUD_HOOK_INIT_OK | SSHUD_HOOK_BUILDID_OK | SSHUD_HOOK_TAKEDAMAGE;
  CHECK(sshud::HudReader::hookDiagnosis(all) == nullptr);  // 全绿时无诊断
}

int main() {
  std::printf("=== sshud seqlock / reader 测试 ===\n");
  testNotAttached();
  testRejectsBadMagic();
  testRejectsVersionMismatch();
  testClampsHostileEntityCount();
  testOddSeqIsTorn();
  testHelpers();
  testHookDiagnosis();
  testConcurrentReadWrite();

  if (gFailures) {
    std::printf("\n%d 项失败\n", gFailures);
    return 1;
  }
  std::printf("\n全部通过\n");
  return 0;
}
