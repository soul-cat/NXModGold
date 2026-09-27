// boss_rush_model.hpp / boss_rush_table.hpp 的宿主机测试
#include "../exl/program/boss_rush_model.hpp"

#include <cstdio>
#include <cstring>

using namespace sshud::bossrush;

static int g_failures = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    if (!(cond)) {                                                  \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                 \
    }                                                               \
  } while (0)

/// 一帧按下 buttons、下一帧全松开；返回按下那一帧的结果
static MenuResult tap(BossMenu& menu, uint64_t buttons, bool canUse = true) {
  const MenuResult result = menu.update(buttons, canUse);
  menu.update(0, canUse);
  return result;
}

static void testOpenCombo() {
  BossMenu menu;
  CHECK(tap(menu, BUTTON_ZR).command == Command::None);  // 只按 ZR 不开
  CHECK(!menu.isOpen());
  menu.update(BUTTON_STICK_R, true);  // 先按住 R3
  CHECK(menu.update(BUTTON_STICK_R | BUTTON_ZR, true).command == Command::Opened);
  CHECK(menu.isOpen());
  CHECK(menu.update(BUTTON_STICK_R | BUTTON_ZR, true).command == Command::None);  // 按住不算再按
  menu.update(0, true);
  BossMenu blocked;
  blocked.update(BUTTON_STICK_R, false);
  CHECK(blocked.update(BUTTON_STICK_R | BUTTON_ZR, false).command == Command::None);  // 不在 4 号位
  CHECK(!blocked.isOpen());
}

static BossMenu openMenu() {
  BossMenu menu;
  menu.update(BUTTON_STICK_R, true);
  menu.update(BUTTON_STICK_R | BUTTON_ZR, true);
  menu.update(0, true);
  return menu;
}

static void testNavigateAndConfirm() {
  BossMenu menu = openMenu();
  CHECK(menu.act() == 1 && menu.cursor() == 0);
  CHECK(menu.itemCount() == FIXED_ITEM_COUNT + bossCountInAct(1));
  tap(menu, BUTTON_UP);  // 向上从第一项绕到最后一项
  CHECK(menu.cursor() == menu.itemCount() - 1);
  tap(menu, BUTTON_DOWN);
  CHECK(menu.cursor() == 0);
  tap(menu, BUTTON_DOWN);
  tap(menu, BUTTON_DOWN);
  const MenuResult travel = tap(menu, BUTTON_A);
  CHECK(travel.command == Command::Travel);
  CHECK(travel.bossIndex == bossIndexInAct(1, 0));
  CHECK(!menu.isOpen());
}

static void testFixedItemsAndClose() {
  BossMenu menu = openMenu();
  CHECK(tap(menu, BUTTON_A).command == Command::Rest);
  menu = openMenu();
  tap(menu, BUTTON_DOWN);
  CHECK(tap(menu, BUTTON_A).command == Command::Refill);
  menu = openMenu();
  CHECK(tap(menu, BUTTON_B).command == Command::Closed);
  menu = openMenu();
  CHECK(menu.update(0, false).command == Command::Closed);  // 切场景 / 离开 4 号位时自动关
}

static void testPagesWrapAndClamp() {
  BossMenu menu = openMenu();
  tap(menu, BUTTON_LEFT);
  CHECK(menu.act() == ACT_COUNT);
  tap(menu, BUTTON_RIGHT);
  CHECK(menu.act() == 1);
  for (int i = 0; i < menu.itemCount() - 1; ++i) tap(menu, BUTTON_DOWN);
  tap(menu, BUTTON_RIGHT);  // 换到 BOSS 更少的一幕：光标夹到最后一项
  CHECK(menu.cursor() == menu.itemCount() - 1);
}

static void testTable() {
  for (size_t i = 0; i < BOSS_COUNT; ++i) {
    const BossEntry& boss = BOSSES[i];
    CHECK(boss.act >= 1 && boss.act <= ACT_COUNT);
    CHECK(boss.flagCount <= MAX_BOSS_FLAGS);
    CHECK(std::strlen(boss.scene) > 0 && std::strlen(boss.gate) > 0);  // 空门名会让画面一直黑着
    CHECK(std::strncmp(boss.nameKey, "NAME_", 5) == 0);
  }
  CHECK(bossIndexInAct(1, bossCountInAct(1)) == -1);
}

static void testBossSlotPath() {
  CHECK(isBossSlotPath("save:/user4.dat"));
  CHECK(isBossSlotPath("save:/user4_1.0.30000.dat"));
  CHECK(isBossSlotPath("save:/Restore_Points4/restoreData19.dat"));
  CHECK(isBossSlotPath("user4.dat"));
  CHECK(!isBossSlotPath("save:/user1.dat"));
  CHECK(!isBossSlotPath("save:/user1_1.0.30000.dat"));
  CHECK(!isBossSlotPath("save:/Restore_Points1/NODELrestoreData1.dat"));
  CHECK(!isBossSlotPath("save:/shared.dat"));
  CHECK(!isBossSlotPath("save:/user40.dat"));
}

int main() {
  testOpenCombo();
  testNavigateAndConfirm();
  testFixedItemsAndClose();
  testPagesWrapAndClamp();
  testTable();
  testBossSlotPath();
  if (g_failures == 0) std::printf("test_boss_rush_model: all passed\n");
  return g_failures == 0 ? 0 : 1;
}
