// HUD 文字（BOSS 名、血量、伤害数字）用的字体。
//
// IMGUI 默认字体没有中文字形，Switch 上中文靠系统字库回退，回退到的是日文字库：日文里也有的字能画（「蕾」），
// 简体独有的字画不出来（「丝」）。游戏自己的 UnityFontFallback 换语言时给它管理的 Font 设 fontNames
// （该语言的系统字库回退顺序；反汇编 UnityFontFallback.OnChangedLanguage 核实），游戏的中文就是这样画全的。
// 所以 HUD 绘制期间把 IMGUI 默认字体（GUIStyle.SetDefaultFont）换成这个 Font，画完还原（ImguiStateGuard）。
//
// 字体取自 FontManager.instance.fallbacks：元素 +0x18 是 Font 的才是 UnityFontFallback
// （TextMeshProFontFallback 同位置是 TMP_FontAsset），按类指针区分。全是字段读取，每帧现取，不跨帧持有托管指针。

#pragma once

#include "boss_detect.hpp"
#include "options_menu_calls.hpp"

namespace sshud::hud {

namespace ui = sshud::unity;

constexpr char16_t FONT_PROBE_CHAR = u'丝';  // 日文字库没有的简体字：日志里核对回退是否到了中文字库

/// FontManager 管理的第一个 Unity Font；找不到返回 null（HUD 退回 IMGUI 默认字体）
inline ui::ManagedObject findGameFont() {
  const void* statics = staticFieldsOf(FONTMGR_TYPEINFO);
  const void* manager = statics != nullptr ? ui::readField<const void*>(statics, FONTMGR_INSTANCE_STATIC) : nullptr;
  const void* list = ui::isAlive(manager) ? ui::readField<const void*>(manager, FONTMGR_FALLBACKS) : nullptr;
  const void* items = list != nullptr ? ui::readField<const void*>(list, IL2CPP_LIST_ITEMS) : nullptr;
  if (items == nullptr) return nullptr;
  const void* fontClass = classOf(FONT_TYPEINFO);
  const int32_t size = ui::readField<int32_t>(list, IL2CPP_LIST_SIZE);
  const auto capacity = static_cast<int32_t>(ui::readField<uint64_t>(items, IL2CPP_ARRAY_LENGTH_FIELD));
  for (int32_t i = 0; i < size && i < capacity; ++i) {
    const void* fallback = ui::readField<const void*>(items, IL2CPP_ARRAY_FIRST_ITEM + i * sizeof(void*));
    void* font = ui::isAlive(fallback) ? ui::readField<void*>(fallback, UFF_FONT) : nullptr;
    if (ui::isAlive(font) && ui::readField<const void*>(font, 0) == fontClass) return font;
  }
  return nullptr;
}

/// 用到的字体变了（首次 / 换语言 / 找不到）才记一行
inline void logGameFont(ui::ManagedObject font) {
  static const void* s_logged = reinterpret_cast<const void*>(-1);
  if (font == s_logged) return;
  s_logged = font;
  if (font == nullptr) return Logging.Log("[sshud] hud font: game font not found, using IMGUI default");
  char name[options::NAME_CAPACITY];
  Logging.Log("[sshud] hud font: %s (has probe glyph=%d)", options::nameOf(font, name, sizeof(name)),
              map::call<bool>(FONT_HASCHARACTER, font, FONT_PROBE_CHAR) ? 1 : 0);
}

inline ui::ManagedObject defaultFont() { return map::call<ui::ManagedObject>(GUISTYLE_GETDEFAULTFONT); }
inline void setDefaultFont(ui::ManagedObject font) { map::call<void>(GUISTYLE_SETDEFAULTFONT, font); }

}  // namespace sshud::hud
