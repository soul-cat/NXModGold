// exl/program/hud_model.hpp 的宿主机单测
#include "../exl/program/hud_model.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace sshud::hud;

static int g_failed = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
      ++g_failed;                                                     \
    }                                                                 \
  } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

static int countActive(const HudModel& model) {
  int n = 0;
  for (int i = 0; i < MAX_ENEMIES; ++i) n += model.enemies()[i].active ? 1 : 0;
  return n;
}

static void testSameEnemyReusesSlot() {
  std::printf("test: 同一敌人复用槽位，最大血量取大\n");
  HudModel model;
  int a = 0;
  model.onHit(&a, 100, 1.0f);
  model.onHit(&a, 90, 2.0f);
  CHECK(countActive(model) == 1);
  CHECK(model.enemies()[0].maxHp == 100);
  CHECK(model.enemies()[0].lastHitAt == 2.0f);
}

static void testEvictsOldestWhenFull() {
  std::printf("test: 槽满时淘汰最久未受击的\n");
  HudModel model;
  int ids[MAX_ENEMIES + 1] = {};
  for (int i = 0; i < MAX_ENEMIES; ++i) model.onHit(&ids[i], 10, 10.0f + i);
  model.onHit(&ids[MAX_ENEMIES], 10, 100.0f);
  CHECK(countActive(model) == MAX_ENEMIES);
  bool firstStillTracked = false;
  bool newTracked = false;
  for (int i = 0; i < MAX_ENEMIES; ++i) {
    firstStillTracked |= model.enemies()[i].manager == &ids[0];
    newTracked |= model.enemies()[i].manager == &ids[MAX_ENEMIES];
  }
  CHECK(!firstStillTracked);
  CHECK(newTracked);
}

static void testExpireAfterHideDelay() {
  std::printf("test: 隐藏延迟后过期，飘字到时过期\n");
  HudModel model;
  int a = 0;
  model.onHit(&a, 50, 1.0f);
  model.addPopup({0, 0, 0}, 5, 1.0f);
  model.expire(1.0f + HIDE_DELAY_SECONDS - 0.01f);
  CHECK(countActive(model) == 1);
  model.expire(1.0f + HIDE_DELAY_SECONDS);
  CHECK(countActive(model) == 0);
  CHECK(model.popups()[0].active);
  model.expire(1.0f + POPUP_DURATION_SECONDS);
  CHECK(!model.popups()[0].active);
}

static void testBossBarDoesNotTimeOut() {
  std::printf("test: BOSS 条不按隐藏延迟过期，只能 forget\n");
  HudModel model;
  int boss = 0;
  model.onHit(&boss, BOSS_HP_THRESHOLD + 1, 1.0f);
  model.expire(1.0f + HIDE_DELAY_SECONDS * 100.0f);
  CHECK(countActive(model) == 1);
  model.forget(&boss);
  CHECK(countActive(model) == 0);
}

static void testForget() {
  std::printf("test: forget 移除指定敌人\n");
  HudModel model;
  int a = 0;
  int b = 0;
  model.onHit(&a, 50, 1.0f);
  model.onHit(&b, 50, 1.0f);
  model.forget(&a);
  CHECK(countActive(model) == 1);
}

static void testBossThreshold() {
  std::printf("test: BOSS 阈值（> 105）\n");
  CHECK(!isBoss(105));
  CHECK(isBoss(106));
}

static void testBossBarLayout() {
  std::printf("test: BOSS 条底部居中、向上堆叠、在屏内\n");
  const Rect r0 = bossBarRect(0, 1920, 1080);
  CHECK(near(r0.width, 900) && near(r0.height, BOSS_BAR_HEIGHT));
  CHECK(near(r0.x, 510));
  CHECK(near(r0.y + r0.height, 1080 - 42));
  const Rect r1 = bossBarRect(1, 1920, 1080);
  CHECK(near(r0.y - r1.y, BOSS_BAR_HEIGHT + 54));
  const Rect small = bossBarRect(0, 1280, 720);
  CHECK(near(small.width, 600));
  CHECK(small.x >= 0 && small.x + small.width <= 1280 && small.y + small.height <= 720);
}

static void testFillRect() {
  std::printf("test: 填充区内缩且按比例截取，比例夹在 [0,1]\n");
  const Rect bar{100, 50, 200, 20};
  const Rect half = fillRect(bar, 0.5f, 2);
  CHECK(near(half.x, 102) && near(half.y, 52) && near(half.width, 98) && near(half.height, 16));
  CHECK(near(fillRect(bar, 2.0f, 2).width, 196));
  CHECK(near(fillRect(bar, -1.0f, 2).width, 0));
}

static void testHealthRatio() {
  std::printf("test: 血量比例的防御性输入\n");
  CHECK(near(healthRatio(65, 130), 0.5f));
  CHECK(near(healthRatio(-3, 130), 0.0f));
  CHECK(near(healthRatio(10, 0), 0.0f));
}

static void testPopupProgress() {
  std::printf("test: 飘字进度单调且夹在 [0,1]\n");
  CHECK(near(popupProgress(1.0f, 1.0f), 0.0f));
  CHECK(near(popupProgress(2.0f, 1.0f), 0.5f));
  CHECK(near(popupProgress(10.0f, 1.0f), 1.0f));
}

static void testPopupPoolRecyclesOldest() {
  std::printf("test: 飘字池满时覆盖最早的\n");
  HudModel model;
  for (int i = 0; i < MAX_POPUPS; ++i) model.addPopup({0, 0, 0}, i, static_cast<float>(i));
  model.addPopup({0, 0, 0}, 999, 100.0f);
  CHECK(model.popups()[0].damage == 999);
}

static void testIsEmpty() {
  std::printf("test: 无敌人无飘字才算空\n");
  HudModel model;
  CHECK(model.isEmpty());
  int a = 0;
  model.onHit(&a, 10, 1.0f);
  CHECK(!model.isEmpty());
  model.forget(&a);
  CHECK(model.isEmpty());
  model.addPopup({0, 0, 0}, 3, 1.0f);
  CHECK(!model.isEmpty());
  model.expire(1.0f + POPUP_DURATION_SECONDS);
  CHECK(model.isEmpty());
}

static void testQuantizeFontSize() {
  std::printf("test: 字号按步长取整，至少 1\n");
  CHECK(quantizeFontSize(24.0f) == 24);
  CHECK(quantizeFontSize(24.9f) == 24);
  CHECK(quantizeFontSize(25.1f) == 26);
  CHECK(quantizeFontSize(0.2f) == 1);
  CHECK(quantizeFontSize(19.2f) == 20 && quantizeFontSize(20.8f) == 20);
}

static void testHealthTextKey() {
  std::printf("test: 血量文字 key 区分 hp 与 maxHp\n");
  CHECK(healthTextKey(5, 10) == healthTextKey(5, 10));
  CHECK(healthTextKey(5, 10) != healthTextKey(10, 5));
  CHECK(healthTextKey(4, 10) != healthTextKey(5, 10));
  CHECK(healthTextKey(0, 1000) != healthTextKey(0, 999));
}

static void testRetryThrottle() {
  std::printf("test: 失败重试按间隔节流\n");
  RetryThrottle throttle;
  CHECK(throttle.tryAcquire(0.0f));
  CHECK(!throttle.tryAcquire(0.5f));
  CHECK(!throttle.tryAcquire(TEXTURE_RETRY_SECONDS - 0.1f));
  CHECK(throttle.tryAcquire(TEXTURE_RETRY_SECONDS));
  CHECK(!throttle.tryAcquire(TEXTURE_RETRY_SECONDS + 0.1f));
}

static void testPlaceholderHp() {
  std::printf("test: 占位血量（SetHP 大数）不计入上限、期间不画，之后的真实血量是新阶段\n");
  CHECK(!isPlaceholderHp(3000));
  CHECK(isPlaceholderHp(9999));
  CHECK(pickMaxHp(9999, 800) == 800);
  CHECK(pickMaxHp(800, 9999) == 800);
  CHECK(pickMaxHp(600, 800) == 800);
  CHECK(pickMaxHp(9999, 5000) == 9999);
  HudModel model;
  int boss = 0;
  model.onHit(&boss, 9999, 1.0f);  // 标题出现时扫到：还在占位段
  Enemy& enemy = model.enemies()[0];
  CHECK(enemy.active && enemy.isBossStyle && enemy.maxHp == 0);
  CHECK(!isDrawable(enemy, 9999));
  model.expire(1.0f + HIDE_DELAY_SECONDS * 100.0f);  // BOSS 样式不按时间过期
  CHECK(enemy.active);
  CHECK(!observeHp(enemy, 9999, 9999));
  CHECK(observeHp(enemy, 800, 9999));  // FSM 设了真实血量
  CHECK(enemy.maxHp == 800 && isDrawable(enemy, 800));
  CHECK(!observeHp(enemy, 500, 9999));
  CHECK(!observeHp(enemy, 9999, 9999));  // 阶段间又设回占位：不画
  CHECK(!isDrawable(enemy, 9999));
  CHECK(observeHp(enemy, 600, 9999));  // 占位段之后：新阶段
  CHECK(enemy.maxHp == 600);
}

static void testSegmentedBoss() {
  std::printf("test: 分段设血量的 BOSS（苍白之母，实测序列）：按 initHp 显示整场一条，段间不消失\n");
  HudModel model;
  int boss = 0;
  model.onHit(&boss, 1204, 100, 1.0f);  // 第一刀：打前 100（这一段），initHp 1204
  Enemy& enemy = model.enemies()[0];
  CHECK(enemy.isBossStyle && enemy.maxHp == 100);
  observeHp(enemy, 91, 1204);
  CHECK(enemy.totalHp == 1204 && enemy.damageTaken == 9);
  CHECK(hpView(enemy, 91).hp == 1195 && hpView(enemy, 91).maxHp == 1204);
  observeHp(enemy, -1, 1204);  // 打穿一段
  observeHp(enemy, 182, 1204);  // FSM 设下一段：累计不回升
  CHECK(enemy.damageTaken == 101 && hpView(enemy, 182).hp == 1103);
  observeHp(enemy, -4, 1204);
  observeHp(enemy, -13, 1204);  // 段间又挨了一刀
  CHECK(enemy.damageTaken == 101 + 186 + 9);
  CHECK(isDrawable(enemy, -13));
  observeHp(enemy, 9999, 1204);  // 占位值不算掉血也不算回血
  observeHp(enemy, 97, 1204);
  CHECK(enemy.damageTaken == 296);
  for (int i = 0; i < 20; ++i) observeHp(enemy, 97 - 60 * (i % 2), 1204);  // 伪造回血不应让显示回升
  CHECK(hpView(enemy, 37).hp >= 0);
}

static void testNormalBossNotSegmented() {
  std::printf("test: 普通 BOSS（initHp = 起始血量）按实时血量显示；普通敌人按 initHp 显示上限\n");
  HudModel model;
  int boss = 0;
  model.onHit(&boss, 500, 500, 1.0f);
  Enemy& enemy = model.enemies()[0];
  observeHp(enemy, 480, 500);
  CHECK(enemy.totalHp == 0 && hpView(enemy, 480).hp == 480 && hpView(enemy, 480).maxHp == 500);
  observeHp(enemy, 0, 500);
  CHECK(observeHp(enemy, 300, 500));  // 打空后又被设了血：新一段
  CHECK(enemy.maxHp == 300);
  int grunt = 0;
  model.onHit(&grunt, 50, 20, 1.0f);  // 普通敌人：之前被打过也按 initHp 显示上限
  CHECK(model.enemies()[1].maxHp == 50 && !model.enemies()[1].isBossStyle);
}

static void testPhasePlan() {
  std::printf("test: 苍白之母段表（FSM Phase Control）：剩余 = 当前血量 + 后面各段，整场单调下降\n");
  CHECK(phaseNumberOf("P1 HP") == 1 && phaseNumberOf("P6 HP") == 6);
  CHECK(phaseNumberOf("P9 HP") == 0 && phaseNumberOf("P1 HPX") == 0 && phaseNumberOf("Hits Total") == 0);
  PhasePlan plan;
  const int32_t silk[] = {100, 192, 240, 110, 242, 340};
  for (int32_t v : silk) plan.hp[plan.count++] = v;
  CHECK(phaseSum(plan, 0) == 1224);
  CHECK(isChunkPlan(plan, 1204));
  PhasePlan thresholds;  // 阈值式：P2 HP = 剩 600 进二阶段
  thresholds.hp[0] = 1000;
  thresholds.hp[1] = 600;
  thresholds.count = 2;
  CHECK(!isChunkPlan(thresholds, 1000));
  PhasePlan gap;  // 编号不连续：中间一段为 0
  gap.hp[0] = 100;
  gap.hp[2] = 100;
  gap.count = 3;
  CHECK(!isChunkPlan(gap, 200));

  HudModel model;
  int boss = 0;
  model.onHit(&boss, 1204, 100, 1.0f);
  Enemy& enemy = model.enemies()[0];
  observeHp(enemy, 91, 1204);
  enemy.phases = plan;  // 运行时由 readPhasePlan 填
  CHECK(hpView(enemy, 91).hp == 91 + 1124 && hpView(enemy, 91).maxHp == 1224);
  // 日志里的真实序列：打穿后 AddHP 下一段（溢出从下一段扣），也有同一帧先挨刀再加血的
  const int32_t seq[] = {11, -1, 182, 5, 236, 14, -1, -13, 97, 20, 0, 231, 2, -1, 324, 86};  // 5 → 236：同帧 −9 +240
  int32_t lastShown = hpView(enemy, 91).hp;
  bool isMonotonic = true;
  for (int32_t hp : seq) {
    observeHp(enemy, hp, 1204);
    const int32_t shown = hpView(enemy, hp).hp;
    isMonotonic = isMonotonic && shown <= lastShown;
    lastShown = shown;
  }
  CHECK(isMonotonic);
  CHECK(enemy.phaseIndex == 5);
  CHECK(hpView(enemy, 0).hp == 0);  // 最后一段打空 = 整条空
}

int main() {
  std::printf("=== HUD 模型测试 ===\n");
  testSameEnemyReusesSlot();
  testEvictsOldestWhenFull();
  testExpireAfterHideDelay();
  testBossBarDoesNotTimeOut();
  testForget();
  testBossThreshold();
  testPlaceholderHp();
  testSegmentedBoss();
  testNormalBossNotSegmented();
  testPhasePlan();
  testBossBarLayout();
  testFillRect();
  testHealthRatio();
  testPopupProgress();
  testPopupPoolRecyclesOldest();
  testIsEmpty();
  testQuantizeFontSize();
  testHealthTextKey();
  testRetryThrottle();
  if (g_failed) {
    std::printf("\n%d 项失败\n", g_failed);
    return EXIT_FAILURE;
  }
  std::printf("\n全部通过\n");
  return EXIT_SUCCESS;
}
