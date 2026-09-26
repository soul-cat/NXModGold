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
  CHECK(!observeHp(enemy, 9999));
  CHECK(observeHp(enemy, 800));  // FSM 设了真实血量
  CHECK(enemy.maxHp == 800 && isDrawable(enemy, 800));
  CHECK(!observeHp(enemy, 500));
  CHECK(!observeHp(enemy, 9999));  // 阶段间又设回占位：不画
  CHECK(!isDrawable(enemy, 9999));
  CHECK(observeHp(enemy, 600));  // 占位段之后：新阶段
  CHECK(enemy.maxHp == 600);
}

static void testPhasedBoss() {
  std::printf("test: 分阶段设血量的 BOSS（苍白之母）：上限不用 initHp，打空后新阶段上限重设\n");
  HudModel model;
  int boss = 0;
  model.onHit(&boss, 1204, 100, 1.0f);  // initHp 是预制体上的 1204，实际这阶段 100 血
  Enemy& enemy = model.enemies()[0];
  CHECK(enemy.isBossStyle && enemy.maxHp == 100);
  CHECK(!observeHp(enemy, 91));
  CHECK(!observeHp(enemy, 0));  // 打空：readPresence 给 Hidden，槽位留着
  CHECK(observeHp(enemy, 213));  // FSM 设下一阶段
  CHECK(enemy.maxHp == 213);
  CHECK(!observeHp(enemy, 150));
  CHECK(!observeHp(enemy, 160));  // 小幅回血：不算新阶段
  CHECK(enemy.maxHp == 213);
  CHECK(observeHp(enemy, 300));  // 超过上限：抬高
  CHECK(enemy.maxHp == 300);
  int grunt = 0;
  model.onHit(&grunt, 50, 20, 1.0f);  // 普通敌人：之前被打过也按 initHp 显示上限
  CHECK(model.enemies()[1].maxHp == 50 && !model.enemies()[1].isBossStyle);
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
  testPhasedBoss();
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
