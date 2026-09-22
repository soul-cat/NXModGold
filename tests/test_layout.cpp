// 血条布局测试。把布局做成纯函数就是为了能在这里测到——
// 实机上只剩 drawRect 调用，不留计算。

#include <cstdio>
#include <cstring>

#include "../overlay/source/hud_layout.hpp"
#include "../overlay/source/saltynx_client.hpp"

using namespace sshud;

static int gFailures = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    if (!(cond)) {                                                  \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
      ++gFailures;                                                  \
    }                                                               \
  } while (0)

static const uint64_t FREQ = 19200000;

static SsHudBlock makeBlock(uint64_t nowTick) {
  SsHudBlock b;
  std::memset(&b, 0, sizeof(b));
  b.magic = SSHUD_MAGIC;
  b.version = SSHUD_VERSION;
  b.tickFreq = FREQ;
  b.frameTick = nowTick;
  return b;
}

static SsHudEntity makeEntity(uint64_t id, int32_t hp, int32_t maxHp, uint64_t hitTick,
                              bool boss) {
  SsHudEntity e;
  std::memset(&e, 0, sizeof(e));
  e.objectId = id;
  e.hp = hp;
  e.maxHp = maxHp;
  e.lastHitTick = hitTick;
  e.lastDamage = 5;
  e.flags = SSHUD_FLAG_ALIVE | (boss ? SSHUD_FLAG_BOSS : 0u);
  return e;
}

static void testFadeCurve() {
  std::printf("test: 淡出曲线\n");
  CHECK(fadeAlphaFor(0.0) == 0xF);
  CHECK(fadeAlphaFor(FADE_SOLID_SECONDS) == 0xF);           // 边界仍全不透明
  CHECK(fadeAlphaFor(FADE_SOLID_SECONDS + FADE_OUT_SECONDS + 0.1) == 0);
  CHECK(fadeAlphaFor(-1.0) == 0xF);                          // 负数按刚受击处理

  // 中点应当大致过半
  const uint8_t mid = fadeAlphaFor(FADE_SOLID_SECONDS + FADE_OUT_SECONDS * 0.5);
  CHECK(mid > 5 && mid < 11);

  // 必须单调不增
  uint8_t prev = 0xF;
  for (double t = 0.0; t < 6.0; t += 0.05) {
    const uint8_t a = fadeAlphaFor(t);
    CHECK(a <= prev);
    prev = a;
  }
}

static void testFillColour() {
  std::printf("test: 血量配色\n");
  const BarColor high = fillColorFor(0.9f);
  const BarColor mid = fillColorFor(0.4f);
  const BarColor low = fillColorFor(0.1f);
  CHECK(high.g > high.r);   // 绿
  CHECK(mid.r > 0xA && mid.g > 0xA);  // 黄
  CHECK(low.r > low.g);     // 红
  // 边界不得抖动
  const BarColor justAboveHalf = fillColorFor(0.51f);
  const BarColor atQuarter = fillColorFor(0.25f);
  CHECK(std::memcmp(&high, &justAboveHalf, sizeof(BarColor)) == 0);
  CHECK(std::memcmp(&low, &atQuarter, sizeof(BarColor)) == 0);
}

static void testFillWidthClamped() {
  std::printf("test: 填充宽度夹取\n");
  SsHudBlock b = makeBlock(FREQ * 100);

  // 满血
  SsHudEntity full = makeEntity(1, 50, 50, FREQ * 100, false);
  BarLayout l = layoutFor(b, full, -1, 0);
  CHECK(l.visible);
  CHECK(l.fillWidth == l.w);

  // 空血
  SsHudEntity empty = makeEntity(2, 0, 50, FREQ * 100, false);
  l = layoutFor(b, empty, -1, 0);
  CHECK(l.fillWidth == 0);

  // 损坏数据：hp 超过 maxHp，不得越界
  SsHudEntity broken = makeEntity(3, 999, 50, FREQ * 100, false);
  l = layoutFor(b, broken, -1, 0);
  CHECK(l.fillWidth == l.w);

  // maxHp 为 0，不得除零
  SsHudEntity zero = makeEntity(4, 10, 0, FREQ * 100, false);
  l = layoutFor(b, zero, -1, 0);
  CHECK(l.fillWidth == 0);

  // 负血
  SsHudEntity neg = makeEntity(5, -20, 50, FREQ * 100, false);
  l = layoutFor(b, neg, -1, 0);
  CHECK(l.fillWidth == 0);
}

static void testBarsStayInsideFramebuffer() {
  std::printf("test: 所有条必须落在 framebuffer 内\n");
  SsHudBlock b = makeBlock(FREQ * 100);
  b.entityCount = SSHUD_MAX_ENTITIES;
  for (uint32_t i = 0; i < SSHUD_MAX_ENTITIES; ++i) {
    b.entities[i] = makeEntity(0x100 + i, 30, 50, FREQ * 100, i < 2);
  }

  BarLayout bars[SSHUD_MAX_ENTITIES];
  const int n = layoutSnapshot(b, bars, SSHUD_MAX_ENTITIES);
  CHECK(n > 0);
  for (int i = 0; i < n; ++i) {
    CHECK(bars[i].x >= 0);
    CHECK(bars[i].y >= 0);
    CHECK(bars[i].x + bars[i].w <= FB_WIDTH);
    CHECK(bars[i].y + bars[i].h <= FB_HEIGHT);
    CHECK(bars[i].fillWidth <= bars[i].w);
  }
}

static void testBarsDoNotOverlap() {
  std::printf("test: 同类血条不得重叠\n");
  SsHudBlock b = makeBlock(FREQ * 100);
  b.entityCount = SSHUD_MAX_ENTITIES;
  for (uint32_t i = 0; i < SSHUD_MAX_ENTITIES; ++i) {
    b.entities[i] = makeEntity(0x200 + i, 30, 50, FREQ * 100 + i, false);
  }

  BarLayout bars[SSHUD_MAX_ENTITIES];
  const int n = layoutSnapshot(b, bars, SSHUD_MAX_ENTITIES);
  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) {
      const bool sameColumn = bars[i].x == bars[j].x;
      if (!sameColumn) continue;
      const bool disjoint = (bars[i].y + bars[i].h <= bars[j].y) ||
                            (bars[j].y + bars[j].h <= bars[i].y);
      CHECK(disjoint);
    }
  }
}

static void testBossAndMobSeparated() {
  std::printf("test: Boss 与普通敌人分列\n");
  SsHudBlock b = makeBlock(FREQ * 100);
  b.entityCount = 4;
  b.entities[0] = makeEntity(1, 300, 500, FREQ * 100, true);
  b.entities[1] = makeEntity(2, 20, 40, FREQ * 100, false);
  b.entities[2] = makeEntity(3, 20, 40, FREQ * 100, false);
  b.entities[3] = makeEntity(4, 400, 500, FREQ * 100, true);

  BarLayout bars[8];
  const int n = layoutSnapshot(b, bars, 8);
  CHECK(n == 4);

  int bossCount = 0, mobCount = 0;
  for (int i = 0; i < n; ++i) {
    if (bars[i].isBoss) {
      ++bossCount;
      CHECK(bars[i].x == BAR_MARGIN_X);   // Boss 走顶部通栏
      CHECK(bars[i].w == BOSS_BAR_W);
    } else {
      ++mobCount;
      CHECK(bars[i].x == MOB_BAR_X);      // 普通敌人走右侧竖列
      CHECK(bars[i].w == MOB_BAR_W);
    }
  }
  CHECK(bossCount == 2);
  CHECK(mobCount == 2);
}

static void testExpiredEntitiesHidden() {
  std::printf("test: 淡出完毕的条不再绘制\n");
  const uint64_t now = FREQ * 1000;
  SsHudBlock b = makeBlock(now);
  b.entityCount = 2;
  // 一个刚受击，一个早就该消失了
  b.entities[0] = makeEntity(1, 30, 50, now, false);
  b.entities[1] = makeEntity(2, 30, 50, now - FREQ * 10, false);

  BarLayout bars[8];
  const int n = layoutSnapshot(b, bars, 8);
  CHECK(n == 1);
  CHECK(bars[0].alpha == 0xF);
}

static void testNewestFirst() {
  std::printf("test: 最近受击的排在前面\n");
  const uint64_t now = FREQ * 1000;
  SsHudBlock b = makeBlock(now);
  b.entityCount = 3;
  b.entities[0] = makeEntity(0xAA, 30, 50, now - FREQ / 2, false);
  b.entities[1] = makeEntity(0xBB, 30, 50, now, false);          // 最新
  b.entities[2] = makeEntity(0xCC, 30, 50, now - FREQ / 4, false);

  BarLayout bars[8];
  const int n = layoutSnapshot(b, bars, 8);
  CHECK(n == 3);
  // 第一条应当是最新的那个，也就是 y 最小的槽位
  CHECK(bars[0].y == mobBarY(0));
  CHECK(bars[1].y == mobBarY(1));
  CHECK(bars[2].y == mobBarY(2));
}

static void testDefensiveInputs() {
  std::printf("test: 防御性输入\n");
  SsHudBlock b = makeBlock(FREQ * 100);
  BarLayout bars[8];

  CHECK(layoutSnapshot(b, nullptr, 8) == 0);
  CHECK(layoutSnapshot(b, bars, 0) == 0);

  // entityCount 被污染成越界值时必须夹取，不得读越界
  b.entityCount = 0xFFFFFFFFu;
  for (uint32_t i = 0; i < SSHUD_MAX_ENTITIES; ++i)
    b.entities[i] = makeEntity(i + 1, 10, 20, FREQ * 100, false);
  const int n = layoutSnapshot(b, bars, 8);
  CHECK(n >= 0 && n <= 8);
}

static void testFramebufferConstraints() {
  std::printf("test: framebuffer 约束\n");
  CHECK(FB_WIDTH % 32 == 0);                 // libtesla swizzle 要求
  const int bytesPerBuffer = FB_WIDTH * FB_HEIGHT * 2;  // RGBA4444
  const int doubleBuffered = bytesPerBuffer * 2;
  CHECK(doubleBuffered < 6 * 1024 * 1024);   // nx-ovlloader 的 applet 堆
  std::printf("    双缓冲占用 %d KB / 6144 KB\n", doubleBuffered / 1024);
}

static void testFindBlockOffset() {
  std::printf("test: 共享内存 magic 扫描\n");
  uint8_t arena[SSHUD_SHMEM_TOTAL];
  std::memset(arena, 0, sizeof(arena));

  CHECK(findBlockOffset(nullptr, SSHUD_SHMEM_TOTAL, SSHUD_MAGIC) == -1);
  CHECK(findBlockOffset(arena, SSHUD_SHMEM_TOTAL, SSHUD_MAGIC) == -1);  // 空区域

  // 在 4 字节对齐的位置放一个 magic
  const int32_t at = 256;
  arena[at + 0] = (uint8_t)(SSHUD_MAGIC & 0xFF);
  arena[at + 1] = (uint8_t)((SSHUD_MAGIC >> 8) & 0xFF);
  arena[at + 2] = (uint8_t)((SSHUD_MAGIC >> 16) & 0xFF);
  arena[at + 3] = (uint8_t)((SSHUD_MAGIC >> 24) & 0xFF);
  CHECK(findBlockOffset(arena, SSHUD_SHMEM_TOTAL, SSHUD_MAGIC) == at);

  // NX-FPS 的 magic 不得被误认
  CHECK(findBlockOffset(arena, SSHUD_SHMEM_TOTAL, 0x465053u) == -1);

  // 块装不下时即使找到 magic 也要拒绝
  std::memset(arena, 0, sizeof(arena));
  const int32_t tooLate = SSHUD_SHMEM_TOTAL - 8;
  arena[tooLate + 0] = (uint8_t)(SSHUD_MAGIC & 0xFF);
  arena[tooLate + 1] = (uint8_t)((SSHUD_MAGIC >> 8) & 0xFF);
  arena[tooLate + 2] = (uint8_t)((SSHUD_MAGIC >> 16) & 0xFF);
  arena[tooLate + 3] = (uint8_t)((SSHUD_MAGIC >> 24) & 0xFF);
  CHECK(findBlockOffset(arena, SSHUD_SHMEM_TOTAL, SSHUD_MAGIC) == -1);
}

int main() {
  std::printf("=== 血条布局测试 ===\n");
  testFindBlockOffset();
  testFadeCurve();
  testFillColour();
  testFillWidthClamped();
  testBarsStayInsideFramebuffer();
  testBarsDoNotOverlap();
  testBossAndMobSeparated();
  testExpiredEntitiesHidden();
  testNewestFirst();
  testDefensiveInputs();
  testFramebufferConstraints();

  if (gFailures) {
    std::printf("\n%d 项失败\n", gFailures);
    return 1;
  }
  std::printf("\n全部通过\n");
  return 0;
}
