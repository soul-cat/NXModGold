// BOSS 挑战菜单上的文字（托管字符串，第一次打开菜单时建好并钉住，之后常驻）。
//
// BOSS 名取猎人日志的本地化文本：LocalisedString 是 {表名, 键} 两个字符串的值类型，原生拼一个传给
// LocalisedString.ToString（跟随游戏语言）。取不到（缺词时是 '!' / '#' 开头的占位）才用表里的后备名；
// 同一个日志条目在表里再次出现（双苔藓之母、狂暴螺蝇、蕾丝二见…）也用后备名，免得菜单里两项同名。

#pragma once

#include "boss_rush_model.hpp"
#include "map_calls.hpp"
#include "options_menu_calls.hpp"
#include "start_crest_model.hpp"

namespace sshud::bossrush {

namespace ui = sshud::unity;

constexpr const char16_t* ACT_TITLES_ZH[ACT_COUNT] = {u"< BOSS 挑战 · 第一幕 >", u"< BOSS 挑战 · 第二幕 >",
                                                      u"< BOSS 挑战 · 第三幕 >"};
constexpr const char16_t* ACT_TITLES_EN[ACT_COUNT] = {u"< Boss Rush - Act 1 >", u"< Boss Rush - Act 2 >",
                                                      u"< Boss Rush - Act 3 >"};
constexpr const char16_t* FIXED_ZH[FIXED_ITEM_COUNT] = {u"回休息点（钟心镇长椅）", u"回满血和丝"};
constexpr const char16_t* FIXED_EN[FIXED_ITEM_COUNT] = {u"Return to rest bench", u"Refill health and silk"};
constexpr const char16_t* HINT_ZH = u"A 进入  B 关闭  左右换幕 · 4 号位不会存档";
constexpr const char16_t* HINT_EN = u"A go  B close  Left/Right act · slot 4 never saves";

/// LocalisedString 的内存布局（值类型：两个托管字符串引用）
struct LocalisedStringValue {
  ui::ManagedString sheet;
  ui::ManagedString key;
};

struct MenuTexts {
  bool isReady = false;
  ui::ManagedString actTitles[ACT_COUNT] = {};
  ui::ManagedString fixedItems[FIXED_ITEM_COUNT] = {};
  ui::ManagedString hint = nullptr;
  ui::ManagedString bossNames[BOSS_COUNT] = {};
};

inline MenuTexts g_texts;

/// 建一个托管字符串并永久钉住（菜单文字常驻，数量固定）
inline ui::ManagedString pinnedText(ui::ManagedString text) {
  if (text != nullptr) ui::pinObject(text);
  return text;
}

inline ui::ManagedString utf16Text(const char16_t* text) {
  return pinnedText(map::call<ui::ManagedString>(MARSHAL_PTRTOSTRUNI, text));
}

inline bool isUsableText(ui::ManagedString text) {
  if (text == nullptr) return false;
  const int32_t length = ui::readField<int32_t>(text, options::STRING_LENGTH);
  const auto* chars = reinterpret_cast<const char16_t*>(static_cast<const uint8_t*>(text) + options::STRING_CHARS);
  return length > 0 && crest::isUsableName(chars, static_cast<size_t>(length));
}

inline ui::ManagedString bossNameText(const BossEntry& boss, bool isChinese) {
  const LocalisedStringValue localised{map::managedString(JOURNAL_SHEET), map::managedString(boss.nameKey)};
  ui::ManagedString text = ui::localisedToString(&localised);
  if (isUsableText(text)) return pinnedText(text);
  Logging.Log("[sshud] boss rush: no journal name for %s, using fallback", boss.nameKey);
  return utf16Text(isChinese ? boss.fallbackZh : boss.fallbackEn);
}

/// 第一次打开菜单时调用
inline void ensureTexts() {
  if (g_texts.isReady) return;
  const bool isChinese = options::isSimplifiedChinese(options::currentLanguage());
  for (int i = 0; i < ACT_COUNT; ++i) g_texts.actTitles[i] = utf16Text((isChinese ? ACT_TITLES_ZH : ACT_TITLES_EN)[i]);
  for (int i = 0; i < FIXED_ITEM_COUNT; ++i) g_texts.fixedItems[i] = utf16Text((isChinese ? FIXED_ZH : FIXED_EN)[i]);
  g_texts.hint = utf16Text(isChinese ? HINT_ZH : HINT_EN);
  for (size_t i = 0; i < BOSS_COUNT; ++i) {
    const BossEntry& boss = BOSSES[i];
    g_texts.bossNames[i] = isRepeatedNameKey(i) ? utf16Text(isChinese ? boss.fallbackZh : boss.fallbackEn)
                                                : bossNameText(boss, isChinese);
  }
  g_texts.isReady = true;
}

/// 当前页第 item 项的文字
inline ui::ManagedString itemText(int act, int item) {
  if (item < FIXED_ITEM_COUNT) return g_texts.fixedItems[item];
  const int index = bossIndexInAct(act, item - FIXED_ITEM_COUNT);
  return index >= 0 ? g_texts.bossNames[index] : nullptr;
}

}  // namespace sshud::bossrush
