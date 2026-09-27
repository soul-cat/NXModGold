// HUD 文字（BOSS 名、血量、伤害数字、BOSS 挑战菜单）用的字体。
//
// IMGUI 默认字体没有中文字形，Switch 上中文靠系统字库回退，回退到的是日文字库：日文里也有的字能画（「蕾」），
// 简体独有的字画不出来（「丝」）。换成 FontManager 管的 Perpetua 也不行：它的回退名单
// ['Perpetua', 'Noto Serif CJK SC', …] 在 IMGUI 里不生效，实机大量中文变成方块（HasCharacter 却报「有」，不可信）。
// 所以直接用带中文字形的那个字体：fonts_assets_chinese.bundle 里的 NotoSerifCJKsc-Regular（资源包核实，
// 西文、数字也全），画 HUD 期间设成 IMGUI 默认字体（GUIStyle.SetDefaultFont），画完还原（ImguiStateGuard）。
//
// 运行时找它：拿 FontManager.instance.fallbacks 里的 Font（UnityFontFallback.font，按类指针区分 TMP 字体）当种子，
// Object.GetType 得到 Font 的 System.Type，Resources.FindObjectsOfTypeAll 列出全部已加载字体，按名字挑。
// 找到后钉住缓存；被卸载（isAlive 为假）就重找，找不到时每 FONT_RETRY_FRAMES 帧重试一次，期间用 IMGUI 默认字体。

#pragma once

#include "boss_detect.hpp"
#include "options_menu_calls.hpp"

#include <cstring>

namespace sshud::hud {

namespace ui = sshud::unity;

/// FontManager 管理的第一个 Unity Font（Perpetua）：只当找 Font 类型的种子；没有返回 null
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

constexpr const char* CJK_FONT_NAME = "NotoSerifCJKsc-Regular";
constexpr int FONT_RETRY_FRAMES = 120;

/// 全部已加载字体里名字是 CJK_FONT_NAME 的；没有返回 null
inline ui::ManagedObject findCjkFont(ui::ManagedObject seed) {
  void* fontType = map::call<void*>(OBJECT_GETTYPE, seed);
  void* fonts = fontType != nullptr ? map::call<void*>(RESOURCES_FINDOBJECTSOFTYPEALL, fontType) : nullptr;
  const uint64_t count = fonts != nullptr ? map::arrayLength(fonts) : 0;
  for (uint64_t i = 0; i < count; ++i) {
    ui::ManagedObject font = map::arrayItem(fonts, i);
    char name[options::NAME_CAPACITY];
    if (ui::isAlive(font) && std::strcmp(options::nameOf(font, name, sizeof(name)), CJK_FONT_NAME) == 0) return font;
  }
  static bool s_isMissLogged = false;
  if (!s_isMissLogged) {
    s_isMissLogged = true;
    Logging.Log("[sshud] hud font: %s not among %llu loaded fonts, using IMGUI default", CJK_FONT_NAME,
                static_cast<unsigned long long>(count));
  }
  return nullptr;
}

/// HUD 用的字体（钉住缓存）；找不到返回 null（用 IMGUI 默认字体）
inline ui::ManagedObject hudFont() {
  static ui::ManagedObject s_font = nullptr;
  static ui::PinHandle s_pin = 0;
  static int s_retryIn = 0;
  if (ui::isAlive(s_font)) return s_font;
  if (s_retryIn > 0) {
    --s_retryIn;
    return nullptr;
  }
  ui::ManagedObject seed = findGameFont();
  s_font = seed != nullptr ? findCjkFont(seed) : nullptr;
  ui::unpinObject(s_pin);
  s_pin = s_font != nullptr ? ui::pinObject(s_font) : 0;
  s_retryIn = s_font != nullptr ? 0 : FONT_RETRY_FRAMES;
  if (s_font != nullptr) Logging.Log("[sshud] hud font: %s", CJK_FONT_NAME);
  return s_font;
}

inline ui::ManagedObject defaultFont() { return map::call<ui::ManagedObject>(GUISTYLE_GETDEFAULTFONT); }
inline void setDefaultFont(ui::ManagedObject font) { map::call<void>(GUISTYLE_SETDEFAULTFONT, font); }

}  // namespace sshud::hud
