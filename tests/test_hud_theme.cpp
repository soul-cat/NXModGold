// 皮肤主题的纯数据选择逻辑（不依赖 Unity / exlaunch）。
#include "../exl/program/hud_theme_model.hpp"

#include <cassert>
#include <cstdio>

using namespace sshud::hud;

int main() {
  assert(selectTheme(false) == ThemeId::Default);
  assert(selectTheme(true) == ThemeId::Skin);

  const BarTheme& defaults = themeFor(ThemeId::Default);
  const BarTheme& skin = themeFor(ThemeId::Skin);

  // 皮肤主题是青蓝色：蓝 > 绿 > 红；低血量是暗红：红 > 蓝。
  assert(skin.fill.b > skin.fill.g && skin.fill.g > skin.fill.r);
  assert(skin.lowHpFill.r > skin.lowHpFill.b);
  assert(skin.fill.a == 1.0f);
  assert(skin.highlight.a > 0.0f);
  assert(defaults.highlight.a == 0.0f);

  // 背景是半透明暗青；菜单 ON 仍是青蓝。
  assert(skin.enemyBg.a > 0.0f && skin.enemyBg.a < 1.0f);
  assert(skin.bossBg.a > 0.0f && skin.bossBg.a < 1.0f);
  assert(skin.menuOn.b > skin.menuOn.r);

  // 默认主题保持旧版配色（略偏暖的灰）。
  assert(defaults.fill.r > defaults.fill.b);
  assert(defaults.lowHpFill.r == defaults.fill.r);
  assert(defaults.lowHpFill.g == defaults.fill.g);

  // 低血量过渡：65% 以上不变，35% 以下完全变成低血量色，中间 smoothstep。
  assert(lowHealthBlend(1.0f) == 0.0f);
  assert(lowHealthBlend(0.65f) == 0.0f);
  assert(lowHealthBlend(0.35f) == 1.0f);
  assert(lowHealthBlend(0.20f) == 1.0f);
  const float middle = lowHealthBlend(0.50f);
  assert(middle > 0.0f && middle < 1.0f);
  const ThemeColor mid = blend(skin.fill, skin.lowHpFill, middle);
  assert(mid.r > skin.fill.r && mid.r < skin.lowHpFill.r);

  // BOSS 名字用金色：红 > 绿 > 蓝。
  assert(skin.bossText.r > skin.bossText.g && skin.bossText.g > skin.bossText.b);
  assert(defaults.bossText.r == defaults.bossText.g);

  setTheme(ThemeId::Skin);
  assert(&activeTheme() == &skin);
  setTheme(ThemeId::Default);
  assert(&activeTheme() == &defaults);

  std::puts("test_hud_theme: all passed");
  return 0;
}
