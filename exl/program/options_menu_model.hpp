// 原生「游戏选项」页里四个开关行的纯逻辑（不依赖 Unity / nn，tests/test_options_menu_model.cpp 覆盖）。
//
// 开关行克隆自游戏自己的开/关选项行，选项文字沿用它的本地化键（如 "MOH_ON" / "MOH_OFF"），
// 所以要从键名认出哪一项是"开"；行标签按游戏语言选中文或英文（游戏字体只在对应语言下有中文字形）。

#pragma once

#include "toggles_model.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sshud::options {

// TeamCherry.Localization.LanguageCode 里的简体中文（ZH_TW / ZH_HK 是繁体，字体里未必有简体字形）
constexpr int32_t LANGUAGE_ZH = 199;
constexpr int32_t LANGUAGE_ZH_CN = 202;
constexpr int32_t LANGUAGE_ZH_SG = 203;

constexpr size_t TOGGLE_OPTION_COUNT = 2;  // 模板行必须恰好两个选项（开 / 关）
constexpr int32_t FALLBACK_ON_INDEX = 1;   // 认不出键名时按"0 关 1 开"猜

/// 注入的行：先是 toggles::FEATURES 各一行开 / 关，最后一行是新存档起始纹章（9 选 1，start_crest.hpp）
constexpr size_t ROW_COUNT = toggles::FEATURE_COUNT + 1;
constexpr size_t CREST_ROW = toggles::FEATURE_COUNT;

/// 行标签，顺序同上
constexpr const char16_t* LABELS_ZH[ROW_COUNT] = {u"小怪血条",    u"BOSS 血条",   u"伤害数字",      u"地图收集物",
                                                  u"R3 快捷面板", u"送货不损坏", u"新存档起始纹章"};
constexpr const char16_t* LABELS_EN[ROW_COUNT] = {u"Enemy HP Bars",          u"Boss HP Bar",   u"Damage Numbers",
                                                  u"Map Collectibles",       u"R3 Quick Panel",
                                                  u"Unbreakable Deliveries", u"Starting Crest (New Saves)"};

/// 行在 config.ini 里的键名（也用于克隆行的 GameObject 名）
inline const char* rowKey(size_t row) {
  return row < toggles::FEATURE_COUNT ? toggles::FEATURES[row].key : toggles::START_CREST_KEY;
}

inline bool isSimplifiedChinese(int32_t languageCode) {
  return languageCode == LANGUAGE_ZH || languageCode == LANGUAGE_ZH_CN || languageCode == LANGUAGE_ZH_SG;
}

inline const char16_t* labelFor(size_t row, int32_t languageCode) {
  return isSimplifiedChinese(languageCode) ? LABELS_ZH[row] : LABELS_EN[row];
}

struct OnIndex {
  int32_t index = FALLBACK_ON_INDEX;
  bool isCertain = false;  // false：两个键都认不出，用的是猜的
};

namespace detail {

inline bool endsWithIgnoreCase(const char* text, const char* suffix) {
  const size_t length = std::strlen(text);
  const size_t suffixLength = std::strlen(suffix);
  if (suffixLength > length) return false;
  for (size_t i = 0; i < suffixLength; ++i) {
    char c = text[length - suffixLength + i];
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c != suffix[i]) return false;
  }
  return true;
}

}  // namespace detail

/// 从两个选项的键名（ASCII）认出"开"是第几项：先认 "OFF" 结尾的，再认 "ON" 结尾的
inline OnIndex onIndexFromKeys(const char* key0, const char* key1) {
  const bool isOff0 = detail::endsWithIgnoreCase(key0, "OFF");
  const bool isOff1 = detail::endsWithIgnoreCase(key1, "OFF");
  if (isOff0 != isOff1) return {isOff0 ? 1 : 0, true};
  const bool isOn0 = detail::endsWithIgnoreCase(key0, "ON");
  const bool isOn1 = detail::endsWithIgnoreCase(key1, "ON");
  if (isOn0 != isOn1) return {isOn0 ? 0 : 1, true};
  return {};
}

/// UTF-16 → ASCII（日志用）；非 ASCII 字符写成 '?'，截断到 capacity - 1
inline void asciiFromUtf16(const char16_t* chars, size_t length, char* out, size_t capacity) {
  if (capacity == 0) return;
  size_t i = 0;
  for (; i < length && i + 1 < capacity; ++i) {
    out[i] = chars[i] < 0x80 ? static_cast<char>(chars[i]) : '?';
  }
  out[i] = '\0';
}

}  // namespace sshud::options
