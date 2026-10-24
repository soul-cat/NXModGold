// 血条主题的纯数据模型（不依赖 Unity / exlaunch，宿主机可单测）。
//
// 皮肤主题的取色来自「丝之歌 x 星见雅」皮肤图集：
//   青蓝 #38D8F8 / #50B0D8 / #20A0E0、暗青 #182830、
//   金色 #405800、暗红 #382028 / #704850。
// 这里只保存颜色；0x8A3A000 之类的地址与主题无关。

#pragma once

namespace sshud::hud {

struct ThemeColor {
  float r;
  float g;
  float b;
  float a;
};

constexpr ThemeColor rgba(int r, int g, int b, float a = 1.0f) {
  return {static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
          static_cast<float>(b) / 255.0f, a};
}

struct BarTheme {
  ThemeColor fill;
  ThemeColor lowHpFill;
  ThemeColor enemyBg;
  ThemeColor bossBg;
  ThemeColor bossFrame;
  ThemeColor textDark;
  ThemeColor textLight;
  ThemeColor bossText;  // BOSS 名字专用（皮肤主题用金色，避免和淡青数字混在一起）
  ThemeColor damage;
  ThemeColor highlight;
  ThemeColor menuBg;
  ThemeColor menuOn;
  ThemeColor menuOff;
};

enum class ThemeId { Default, Skin };

// 与旧版硬编码值一致；highlight.a = 0 表示不画 1px 高光。
constexpr BarTheme kDefaultTheme{
    .fill = rgba(0xBE, 0xB8, 0xB8),
    .lowHpFill = rgba(0xBE, 0xB8, 0xB8),
    .enemyBg = rgba(0, 0, 0, 0x85 / 255.0f),
    .bossBg = rgba(0, 0, 0, 0x80 / 255.0f),
    .bossFrame = rgba(0xFF, 0xFF, 0xFF, 0.95f),
    .textDark = rgba(0x0E, 0x04, 0x04),
    .textLight = rgba(0xFF, 0xFF, 0xFF),
    .bossText = rgba(0xFF, 0xFF, 0xFF),
    .damage = rgba(0xDC, 0x14, 0x3C),
    .highlight = rgba(0xFF, 0xFF, 0xFF, 0.0f),
    .menuBg = rgba(0, 0, 0, 0.7f),
    .menuOn = rgba(0x8C, 0xE5, 0x8C),
    .menuOff = rgba(0x99, 0x99, 0x99),
};

// 检测到 SilkModLoader 时使用：青蓝填充 + 暗青底 + 淡青外框 + 暗红低血量 + 金色由菜单沿用。
constexpr BarTheme kSkinTheme{
    .fill = rgba(0x55, 0xC8, 0xF0),
    .lowHpFill = rgba(0xC0, 0x30, 0x50),
    .enemyBg = rgba(0x0A, 0x14, 0x18, 0.80f),
    .bossBg = rgba(0x0A, 0x14, 0x18, 0.78f),
    .bossFrame = rgba(0xD8, 0xF4, 0xFF, 0.95f),
    .textDark = rgba(0x08, 0x14, 0x18),
    .textLight = rgba(0xE8, 0xFA, 0xFF),
    .bossText = rgba(0xE8, 0xC4, 0x6A),
    .damage = rgba(0xFF, 0x4A, 0x6A),
    .highlight = rgba(0xE8, 0xFA, 0xFF, 0.55f),
    .menuBg = rgba(0x08, 0x14, 0x18, 0.78f),
    .menuOn = rgba(0x58, 0xC8, 0xF0),
    .menuOff = rgba(0x7A, 0x8A, 0x90),
};

constexpr const BarTheme& themeFor(ThemeId id) {
  return id == ThemeId::Skin ? kSkinTheme : kDefaultTheme;
}

constexpr ThemeId selectTheme(bool skinLoaderPresent) {
  return skinLoaderPresent ? ThemeId::Skin : ThemeId::Default;
}

// 血量从 65% 降到 35% 时，填充色/文字色在这段区间里平滑过渡；
// 高于 65% 保持正常色，低于 35% 完全变成低血量色。避免在 49% 处硬切。
constexpr float LOW_HEALTH_FADE_START = 0.65f;
constexpr float LOW_HEALTH_FADE_END = 0.35f;

constexpr float clamp01(float value) {
  return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

/// 0 = 高血量（正常色），1 = 低血量（低血量色），中间用 smoothstep 缓动。
constexpr float lowHealthBlend(float ratio) {
  const float t = clamp01((LOW_HEALTH_FADE_START - ratio) /
                          (LOW_HEALTH_FADE_START - LOW_HEALTH_FADE_END));
  return t * t * (3.0f - 2.0f * t);
}

constexpr ThemeColor blend(const ThemeColor& high, const ThemeColor& low, float t) {
  return {high.r + (low.r - high.r) * t, high.g + (low.g - high.g) * t,
          high.b + (low.b - high.b) * t, high.a + (low.a - high.a) * t};
}

// 启动时决定一次；绘制都在游戏主线程，读的时候不再加锁。
inline ThemeId g_themeId = ThemeId::Default;

inline const BarTheme& activeTheme() { return themeFor(g_themeId); }

inline void setTheme(ThemeId id) { g_themeId = id; }

}  // namespace sshud::hud
