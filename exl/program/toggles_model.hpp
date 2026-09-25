// 游戏内功能开关的纯逻辑（不依赖 Unity / nn，tests/test_toggles.cpp 覆盖）。
//
// 按住 R3（右摇杆按下）弹出开关面板，按住期间按方向键切换对应功能：
//   ↑ 小怪血条   → BOSS 血条   ↓ 伤害数字   ← 地图收集物标记
// 松开 R3 面板消失；有改动就写回 SD:/config/sshud/config.ini（每行 key=0/1）。
// 地图标记在下次打开地图时生效（标记只在打开地图时刷新）。
// 前 PANEL_FEATURE_COUNT 个功能 R3 面板与原生选项页都能开关；之后的（button = 0）只在原生选项页里。

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace sshud::toggles {

// nn::hid::NpadButton 的位
constexpr uint64_t BUTTON_STICK_R = 1ull << 5;
constexpr uint64_t BUTTON_LEFT = 1ull << 12;
constexpr uint64_t BUTTON_UP = 1ull << 13;
constexpr uint64_t BUTTON_RIGHT = 1ull << 14;
constexpr uint64_t BUTTON_DOWN = 1ull << 15;

constexpr uint64_t MENU_HOLD = BUTTON_STICK_R;

enum class Feature : uint8_t { EnemyBars, BossBar, DamageNumbers, MapMarkers, ProtectDelivery, Count, None = Count };
constexpr size_t FEATURE_COUNT = static_cast<size_t>(Feature::Count);
constexpr size_t PANEL_FEATURE_COUNT = 4;  // R3 面板只有四个方向键

struct FeatureInfo {
  uint64_t button;  // 0：R3 面板里没有
  const char* key;  // config.ini 里的键名（面板上的中文文字是贴图，见 menu_textures.hpp）
};

constexpr FeatureInfo FEATURES[FEATURE_COUNT] = {
    {BUTTON_UP, "enemy_bars"},
    {BUTTON_RIGHT, "boss_bar"},
    {BUTTON_DOWN, "damage_numbers"},
    {BUTTON_LEFT, "map_markers"},
    {0, "protect_delivery"},  // 送货物品不损坏（delivery_guard.hpp）
};

constexpr const char* CONFIG_HEADER =
    "# sshud feature toggles: 1 = on, 0 = off (in game: Options > Game, or hold R3 + D-pad)\n";
constexpr size_t CONFIG_TEXT_CAPACITY = 512;
constexpr size_t CONFIG_LINE_CAPACITY = 64;

inline const FeatureInfo& infoOf(Feature feature) { return FEATURES[static_cast<size_t>(feature)]; }

struct Settings {
  bool enabled[FEATURE_COUNT] = {true, true, true, true, false};  // HUD 功能默认开，送货保护默认关

  bool isEnabled(Feature feature) const { return enabled[static_cast<size_t>(feature)]; }
  void flip(Feature feature) {
    if (feature != Feature::None) enabled[static_cast<size_t>(feature)] = !isEnabled(feature);
  }
};

/// 每帧一次的面板输入结果
struct MenuEvent {
  bool isOpen = false;             // 本帧 R3 按住中（画面板）
  Feature flipped = Feature::None;  // 本帧切换的功能（只认按下沿）
  bool isClosed = false;           // 本帧刚松开 R3
};

class MenuInput {
 public:
  MenuEvent update(uint64_t buttons) {
    const uint64_t pressed = buttons & ~previous_;
    const bool wasOpen = (previous_ & MENU_HOLD) != 0;
    previous_ = buttons;
    MenuEvent event;
    event.isOpen = (buttons & MENU_HOLD) != 0;
    event.isClosed = wasOpen && !event.isOpen;
    if (!event.isOpen) return event;
    for (size_t i = 0; i < FEATURE_COUNT; ++i) {
      if ((pressed & FEATURES[i].button) != 0) {
        event.flipped = static_cast<Feature>(i);
        break;
      }
    }
    return event;
  }

 private:
  uint64_t previous_ = 0;
};

/// 序列化成 config.ini 文本；返回写入的长度
inline size_t formatSettings(const Settings& settings, char* out, size_t capacity) {
  size_t length = 0;
  auto append = [&](const char* text) {
    const int written = std::snprintf(out + length, capacity - length, "%s", text);
    if (written > 0) length += std::min(static_cast<size_t>(written), capacity - length - 1);
  };
  append(CONFIG_HEADER);
  for (size_t i = 0; i < FEATURE_COUNT; ++i) {
    char line[CONFIG_LINE_CAPACITY];
    std::snprintf(line, sizeof(line), "%s=%d\n", FEATURES[i].key, settings.enabled[i] ? 1 : 0);
    append(line);
  }
  return length;
}

namespace detail {

inline bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

/// 解析一行 "key = value"；不认识的键、注释、坏值都忽略
inline void parseLine(const char* begin, const char* end, Settings& settings) {
  while (begin < end && isSpace(*begin)) ++begin;
  const char* equals = static_cast<const char*>(std::memchr(begin, '=', static_cast<size_t>(end - begin)));
  if (equals == nullptr || *begin == '#') return;
  const char* keyEnd = equals;
  while (keyEnd > begin && isSpace(keyEnd[-1])) --keyEnd;
  const char* value = equals + 1;
  while (value < end && isSpace(*value)) ++value;
  if (value == end || (*value != '0' && *value != '1')) return;
  const size_t keyLength = static_cast<size_t>(keyEnd - begin);
  for (size_t i = 0; i < FEATURE_COUNT; ++i) {
    if (std::strlen(FEATURES[i].key) == keyLength && std::memcmp(FEATURES[i].key, begin, keyLength) == 0) {
      settings.enabled[i] = *value == '1';
    }
  }
}

}  // namespace detail

/// 从 config.ini 文本读开关；文件里没写的功能保持 settings 原值
inline void parseSettings(const char* text, size_t length, Settings& settings) {
  const char* cursor = text;
  const char* end = text + length;
  while (cursor < end) {
    const char* newline = static_cast<const char*>(std::memchr(cursor, '\n', static_cast<size_t>(end - cursor)));
    const char* lineEnd = newline != nullptr ? newline : end;
    detail::parseLine(cursor, lineEnd, settings);
    cursor = lineEnd + 1;
  }
}

}  // namespace sshud::toggles
