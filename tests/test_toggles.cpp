// exl/program/toggles_model.hpp 的宿主机单测
#include "../exl/program/toggles_model.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace sshud::toggles;

static int g_failed = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
      ++g_failed;                                                     \
    }                                                                 \
  } while (0)

/// 解析一段 C 字符串（不含结尾 NUL）
static void parseText(const char* text, Settings& settings) { parseSettings(text, std::strlen(text), settings); }

/// 比较两份开关是否完全一致
static bool isSameSettings(const Settings& a, const Settings& b) {
  for (size_t i = 0; i < FEATURE_COUNT; ++i) {
    if (a.enabled[i] != b.enabled[i]) return false;
  }
  return true;
}

// ---------------------------------------------------------------- MenuInput

static void testMenuNeedsHold() {
  std::printf("test: 不按住 R3 时方向键不切换，面板不打开\n");
  MenuInput input;
  MenuEvent event = input.update(BUTTON_UP);
  CHECK(!event.isOpen && event.flipped == Feature::None && !event.isClosed);
  event = input.update(0);
  CHECK(!event.isOpen && event.flipped == Feature::None && !event.isClosed);
  event = input.update(BUTTON_DOWN | BUTTON_LEFT);
  CHECK(!event.isOpen && event.flipped == Feature::None);
}

static void testMenuOpensWhileHeld() {
  std::printf("test: 按住 R3 时面板打开\n");
  MenuInput input;
  MenuEvent event = input.update(MENU_HOLD);
  CHECK(event.isOpen && event.flipped == Feature::None && !event.isClosed);
  event = input.update(MENU_HOLD);
  CHECK(event.isOpen && !event.isClosed);
}

static void testMenuDirectionMapping() {
  std::printf("test: 上右下左分别切换 小怪血条/BOSS 血条/伤害数字/地图标记\n");
  const struct {
    uint64_t button;
    Feature feature;
  } cases[] = {
      {BUTTON_UP, Feature::EnemyBars},
      {BUTTON_RIGHT, Feature::BossBar},
      {BUTTON_DOWN, Feature::DamageNumbers},
      {BUTTON_LEFT, Feature::MapMarkers},
  };
  for (const auto& c : cases) {
    MenuInput input;
    input.update(MENU_HOLD);
    const MenuEvent event = input.update(MENU_HOLD | c.button);
    CHECK(event.isOpen && event.flipped == c.feature);
  }
}

static void testMenuNoRepeatWhileHeld() {
  std::printf("test: 方向键按住不重复触发，松开再按才再次触发\n");
  MenuInput input;
  input.update(MENU_HOLD);
  CHECK(input.update(MENU_HOLD | BUTTON_UP).flipped == Feature::EnemyBars);
  CHECK(input.update(MENU_HOLD | BUTTON_UP).flipped == Feature::None);
  CHECK(input.update(MENU_HOLD | BUTTON_UP).flipped == Feature::None);
  CHECK(input.update(MENU_HOLD).flipped == Feature::None);
  CHECK(input.update(MENU_HOLD | BUTTON_UP).flipped == Feature::EnemyBars);
  CHECK(input.update(MENU_HOLD | BUTTON_UP | BUTTON_DOWN).flipped == Feature::DamageNumbers);
}

static void testMenuPressedTogether() {
  std::printf("test: R3 与方向键同一帧按下也算\n");
  MenuInput input;
  const MenuEvent event = input.update(MENU_HOLD | BUTTON_RIGHT);
  CHECK(event.isOpen && event.flipped == Feature::BossBar);
}

static void testMenuClosedEdge() {
  std::printf("test: 只在松开 R3 的那一帧 isClosed 为真\n");
  MenuInput input;
  input.update(MENU_HOLD);
  input.update(MENU_HOLD | BUTTON_LEFT);
  MenuEvent event = input.update(0);
  CHECK(!event.isOpen && event.isClosed && event.flipped == Feature::None);
  event = input.update(0);
  CHECK(!event.isOpen && !event.isClosed);
  event = input.update(BUTTON_UP);
  CHECK(!event.isClosed && event.flipped == Feature::None);
}

static void testMenuReleaseWithoutOpen() {
  std::printf("test: 没打开过面板时松开按键不算关闭\n");
  MenuInput input;
  CHECK(!input.update(BUTTON_UP).isClosed);
  CHECK(!input.update(0).isClosed);
  CHECK(!input.update(0).isClosed);
}

// ---------------------------------------------------------------- Settings

static void testSettingsDefaultsAndFlip() {
  std::printf("test: HUD 功能默认开、任务作弊默认关，flip 翻转，flip(None) 无操作\n");
  Settings settings;
  for (size_t i = 0; i < PANEL_FEATURE_COUNT; ++i) CHECK(settings.isEnabled(static_cast<Feature>(i)));
  CHECK(!settings.isEnabled(Feature::SkipGourmandMeat));
  settings.flip(Feature::BossBar);
  CHECK(!settings.isEnabled(Feature::BossBar));
  CHECK(settings.isEnabled(Feature::EnemyBars) && settings.isEnabled(Feature::DamageNumbers) &&
        settings.isEnabled(Feature::MapMarkers));
  settings.flip(Feature::BossBar);
  CHECK(settings.isEnabled(Feature::BossBar));
  const Settings before = settings;
  settings.flip(Feature::None);
  CHECK(isSameSettings(before, settings));
}

// ---------------------------------------------------------------- formatSettings / parseSettings

static void testFormatSettingsDefaults() {
  std::printf("test: 默认开关序列化文本精确匹配\n");
  Settings settings;
  char text[CONFIG_TEXT_CAPACITY];
  const size_t length = formatSettings(settings, text, sizeof(text));
  const char* expected =
      "# sshud feature toggles: 1 = on, 0 = off (in game: Options > Game, or hold R3 + D-pad)\n"
      "enemy_bars=1\n"
      "boss_bar=1\n"
      "damage_numbers=1\n"
      "map_markers=1\n"
      "skip_gourmand_meat=0\n";
  CHECK(std::strcmp(text, expected) == 0);
  CHECK(length == std::strlen(expected));
}

static void testFormatSettingsTruncates() {
  std::printf("test: 序列化容量不足时截断并以 NUL 结尾\n");
  Settings settings;
  constexpr size_t SMALL_CAPACITY = 16;
  char text[SMALL_CAPACITY];
  const size_t length = formatSettings(settings, text, sizeof(text));
  CHECK(length == SMALL_CAPACITY - 1);
  CHECK(text[SMALL_CAPACITY - 1] == '\0');
  CHECK(std::strlen(text) == length);
}

static void testSettingsRoundTrip() {
  std::printf("test: 序列化后再解析得到同样的开关\n");
  Settings original;
  original.flip(Feature::BossBar);
  original.flip(Feature::MapMarkers);
  char text[CONFIG_TEXT_CAPACITY];
  const size_t length = formatSettings(original, text, sizeof(text));
  Settings parsed;
  parseSettings(text, length, parsed);
  CHECK(isSameSettings(original, parsed));
  CHECK(parsed.isEnabled(Feature::EnemyBars) && !parsed.isEnabled(Feature::BossBar));
  CHECK(parsed.isEnabled(Feature::DamageNumbers) && !parsed.isEnabled(Feature::MapMarkers));

  // 全关也能还原
  Settings allOff;
  for (size_t i = 0; i < FEATURE_COUNT; ++i) allOff.flip(static_cast<Feature>(i));
  const size_t offLength = formatSettings(allOff, text, sizeof(text));
  Settings parsedOff;
  parseSettings(text, offLength, parsedOff);
  CHECK(isSameSettings(allOff, parsedOff));
}

static void testParseCrlfAndSpaces() {
  std::printf("test: 解析容忍 CRLF 换行和键/等号两侧空白\n");
  Settings settings;
  parseText("enemy_bars=0\r\n  boss_bar = 0 \r\n\tdamage_numbers\t=\t0\r\nmap_markers =0\r\n", settings);
  CHECK(!settings.isEnabled(Feature::EnemyBars));
  CHECK(!settings.isEnabled(Feature::BossBar));
  CHECK(!settings.isEnabled(Feature::DamageNumbers));
  CHECK(!settings.isEnabled(Feature::MapMarkers));
}

static void testParseIgnoresComments() {
  std::printf("test: # 开头的注释行被忽略（包括 #enemy_bars=0）\n");
  Settings settings;
  parseText("# comment = 0\n#enemy_bars=0\n   # boss_bar=0\n", settings);
  CHECK(isSameSettings(settings, Settings{}));
}

static void testParseIgnoresUnknownAndBadValues() {
  std::printf("test: 未知键、坏值被忽略，保留原值\n");
  Settings settings;
  settings.flip(Feature::DamageNumbers);  // 原值：damage_numbers 关
  parseText("unknown=0\nenemy_bars=2\nboss_bar=\nmap_markers=yes\ndamage_numbers=\nno_equals_line\n=1\n",
            settings);
  CHECK(settings.isEnabled(Feature::EnemyBars));
  CHECK(settings.isEnabled(Feature::BossBar));
  CHECK(!settings.isEnabled(Feature::DamageNumbers));
  CHECK(settings.isEnabled(Feature::MapMarkers));
}

static void testParseMissingTrailingNewline() {
  std::printf("test: 最后一行没有换行也能解析\n");
  Settings settings;
  parseText("enemy_bars=1\nmap_markers=0", settings);
  CHECK(settings.isEnabled(Feature::EnemyBars));
  CHECK(!settings.isEnabled(Feature::MapMarkers));

  // 只按 length 解析，不读到 length 之后
  Settings bounded;
  const char text[] = "boss_bar=0boss_bar=1";
  parseSettings(text, std::strlen("boss_bar=0"), bounded);
  CHECK(!bounded.isEnabled(Feature::BossBar));
}

static void testParseAbsentKeysKeepPrior() {
  std::printf("test: 文件里没写的键保持原值\n");
  Settings settings;
  settings.flip(Feature::EnemyBars);
  settings.flip(Feature::MapMarkers);
  parseText("boss_bar=0\n", settings);
  CHECK(!settings.isEnabled(Feature::EnemyBars));
  CHECK(!settings.isEnabled(Feature::BossBar));
  CHECK(settings.isEnabled(Feature::DamageNumbers));
  CHECK(!settings.isEnabled(Feature::MapMarkers));

  Settings empty;
  parseSettings("", 0, empty);
  CHECK(isSameSettings(empty, Settings{}));
}

static void testParseKeyMustMatchExactly() {
  std::printf("test: 键名的前缀/后缀（boss_bar_x、bar）不能误匹配\n");
  Settings settings;
  parseText("boss_bar_x=0\nbar=0\nboss=0\nxenemy_bars=0\nenemy_bar=0\nmap_markers_=0\nBOSS_BAR=0\n", settings);
  CHECK(isSameSettings(settings, Settings{}));
}

int main() {
  std::printf("=== 功能开关测试 ===\n");
  testMenuNeedsHold();
  testMenuOpensWhileHeld();
  testMenuDirectionMapping();
  testMenuNoRepeatWhileHeld();
  testMenuPressedTogether();
  testMenuClosedEdge();
  testMenuReleaseWithoutOpen();
  testSettingsDefaultsAndFlip();
  testFormatSettingsDefaults();
  testFormatSettingsTruncates();
  testSettingsRoundTrip();
  testParseCrlfAndSpaces();
  testParseIgnoresComments();
  testParseIgnoresUnknownAndBadValues();
  testParseMissingTrailingNewline();
  testParseAbsentKeysKeepPrior();
  testParseKeyMustMatchExactly();
  if (g_failed) {
    std::printf("\n%d 项失败\n", g_failed);
    return EXIT_FAILURE;
  }
  std::printf("\n全部通过\n");
  return EXIT_SUCCESS;
}
