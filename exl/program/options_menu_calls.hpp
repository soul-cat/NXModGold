// 原生选项页注入用到的托管调用薄封装（IL2CPP 原生调用：参数后追加 MethodInfo* = null）。
// 通用的 Transform / GameObject 封装沿用 map_calls.hpp。

#pragma once

#include "map_calls.hpp"
#include "options_menu_model.hpp"

#include <cstdio>

namespace sshud::options {

namespace ui = sshud::unity;
using map::call;
using map::childAt;
using map::childCount;
using map::componentByName;
using map::gameObjectOf;
using map::transformOf;

constexpr int32_t NAVIGATION_EXPLICIT = 4;  // UnityEngine.UI.Navigation.Mode.Explicit
constexpr int32_t APPLY_ON_SCROLL = 0;      // MenuOptionHorizontal.ApplyOnType.Scroll
constexpr uintptr_t STRING_LENGTH = 0x10;   // System.String：int32 长度后紧跟 UTF-16 字符
constexpr uintptr_t STRING_CHARS = 0x14;
constexpr uintptr_t LIST_ITEMS = 0x10;      // List<T>：T[] _items; int _size
constexpr uintptr_t LIST_SIZE = 0x18;
constexpr size_t NAME_CAPACITY = 48;

/// UnityEngine.UI.Navigation（40 字节，按值传参/返回时编译器自动走指针 / x8，与 IL2CPP 一致）
struct Navigation {
  int32_t mode;
  bool wrapAround;
  ui::ManagedObject selectOnUp;
  ui::ManagedObject selectOnDown;
  ui::ManagedObject selectOnLeft;
  ui::ManagedObject selectOnRight;
};
static_assert(sizeof(Navigation) == 40, "Navigation 布局须与 UnityEngine.UI.Navigation 一致");

inline Navigation navigationOf(ui::ManagedObject selectable) { return call<Navigation>(SEL_GETNAVIGATION, selectable); }
inline void setNavigation(ui::ManagedObject selectable, Navigation nav) { call<void>(SEL_SETNAVIGATION, selectable, nav); }

inline ui::ManagedObject parentOf(ui::ManagedObject tr) { return call<ui::ManagedObject>(TR_GETPARENT, tr); }
inline int32_t siblingIndexOf(ui::ManagedObject tr) { return call<int32_t>(TR_GETSIBLINGINDEX, tr); }
inline void setSiblingIndex(ui::ManagedObject tr, int32_t i) { call<void>(TR_SETSIBLINGINDEX, tr, i); }
inline bool isActiveSelf(ui::ManagedObject go) { return call<bool>(GO_GETACTIVESELF, go); }
inline ui::Vector3 localPositionOf(ui::ManagedObject tr) { return call<ui::Vector3>(TR_GETLOCALPOSITION, tr); }
inline void setLocalPosition(ui::ManagedObject tr, ui::Vector3 p) { call<void>(TR_SETLOCALPOSITION, tr, p); }
inline void destroyNow(ui::ManagedObject object) { call<void>(UOBJ_DESTROYIMMEDIATE, object); }

inline void setText(ui::ManagedObject text, const char16_t* value) {
  call<void>(TEXT_SETTEXT, text, call<ui::ManagedString>(MARSHAL_PTRTOSTRUNI, value));
}
inline int32_t currentLanguage() { return call<int32_t>(FM_GETCURRENTLANGUAGE); }
inline void setOptionTo(ui::ManagedObject option, int32_t index) { call<void>(MOH_SETOPTIONTO, option, index); }

/// 托管字符串 → ASCII（日志用），null 写成 "(null)"
inline const char* asciiOf(ui::ManagedString text, char* out, size_t capacity) {
  if (text == nullptr) {
    std::snprintf(out, capacity, "(null)");
    return out;
  }
  const int32_t length = ui::readField<int32_t>(text, STRING_LENGTH);
  const auto* chars = reinterpret_cast<const char16_t*>(static_cast<const uint8_t*>(text) + STRING_CHARS);
  asciiFromUtf16(chars, length > 0 ? static_cast<size_t>(length) : 0, out, capacity);
  return out;
}

/// Unity 对象的名字（ASCII，日志用）
inline const char* nameOf(ui::ManagedObject object, char* out, size_t capacity) {
  if (!ui::isAlive(object)) {
    std::snprintf(out, capacity, "(dead)");
    return out;
  }
  return asciiOf(call<ui::ManagedString>(UOBJ_GETNAME, object), out, capacity);
}

/// tr 是否是 ancestor 自己或它的后代
inline bool isUnder(ui::ManagedObject tr, ui::ManagedObject ancestor) {
  for (ui::ManagedObject at = tr; at != nullptr; at = parentOf(at)) {
    if (at == ancestor) return true;
  }
  return false;
}

/// label 的字体里缺几个字（字体为空返回 -1）
inline int32_t missingGlyphs(ui::ManagedObject text, const char16_t* label) {
  ui::ManagedObject font = call<ui::ManagedObject>(TEXT_GETFONT, text);
  if (!ui::isAlive(font)) return -1;
  int32_t missing = 0;
  for (const char16_t* c = label; *c != 0; ++c) {
    if (!call<bool>(FONT_HASCHARACTER, font, *c)) ++missing;
  }
  return missing;
}

}  // namespace sshud::options
