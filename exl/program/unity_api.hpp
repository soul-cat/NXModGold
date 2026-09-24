// 从原生代码直接调用 IL2CPP 编译后的 Unity / .NET 方法。
//
// IL2CPP 方法的原生签名 = C# 参数 + 末尾一个 const MethodInfo*；实例方法首参是 this，
// 值类型的实例方法 this 是指向未装箱值的指针。非泛型方法不读 MethodInfo*，传 nullptr。
// Vector3 / Color / Rect 都是 float 的同质聚合（HFA），AAPCS64 下经 s 寄存器传参/返回，
// 与下面的 C++ 结构体一致，所以可以按值传。
//
// 只能在 Unity 主线程上调用（TakeDamage、LateUpdate、OnGUI 都在主线程）。
// 托管对象（字符串等）是 GC 管理的，本模块不持有引用 —— 只在当帧内使用，绝不跨帧缓存。

#pragma once

#include "lib.hpp"
#include "offsets.h"

#include <cstdint>
#include <cstring>

namespace sshud::unity {

struct Vector3 {
  float x;
  float y;
  float z;
};

struct Color {
  float r;
  float g;
  float b;
  float a;
};

struct Rect {
  float x;
  float y;
  float width;
  float height;
};

using ManagedString = void*;
using ManagedObject = void*;

template <typename Fn>
Fn mainFunction(uintptr_t rva) {
  return reinterpret_cast<Fn>(exl::util::modules::GetTargetOffset(rva));
}

template <typename T>
T readField(const void* base, uintptr_t offset) {
  T value{};
  std::memcpy(&value, static_cast<const uint8_t*>(base) + offset, sizeof(T));
  return value;
}

/// UnityEngine.Object 被 Destroy 后 m_CachedPtr 清零；对它调任何 Unity API 都会抛异常。
inline bool isAlive(const void* unityObject) {
  return unityObject != nullptr && readField<const void*>(unityObject, OBJ_CACHEDPTR) != nullptr;
}

// ---------------------------------------------------------------- 场景 / 相机

inline Vector3 worldPositionOf(const void* component) {
  auto getTransform = mainFunction<ManagedObject (*)(const void*, const void*)>(COMP_GETTRANSFORM);
  auto getPosition = mainFunction<Vector3 (*)(ManagedObject, const void*)>(TR_GETPOSITION);
  return getPosition(getTransform(component, nullptr), nullptr);
}

inline ManagedObject mainCamera() {
  return mainFunction<ManagedObject (*)(const void*)>(CAM_GETMAIN)(nullptr);
}

/// 相机像素坐标，原点左下；z 为到相机的距离（< 0 在相机背后）
inline Vector3 worldToScreen(ManagedObject camera, Vector3 world) {
  return mainFunction<Vector3 (*)(ManagedObject, Vector3, const void*)>(CAM_WORLDTOSCREEN)(camera, world,
                                                                                           nullptr);
}

inline int32_t cameraPixelWidth(ManagedObject camera) {
  return mainFunction<int32_t (*)(ManagedObject, const void*)>(CAM_PIXELWIDTH)(camera, nullptr);
}

inline int32_t cameraPixelHeight(ManagedObject camera) {
  return mainFunction<int32_t (*)(ManagedObject, const void*)>(CAM_PIXELHEIGHT)(camera, nullptr);
}

inline int32_t screenWidth() { return mainFunction<int32_t (*)(const void*)>(SCREEN_GETWIDTH)(nullptr); }
inline int32_t screenHeight() { return mainFunction<int32_t (*)(const void*)>(SCREEN_GETHEIGHT)(nullptr); }
inline float unscaledTime() { return mainFunction<float (*)(const void*)>(TIME_GETUNSCALEDTIME)(nullptr); }

// ---------------------------------------------------------------- IMGUI

constexpr int32_t EVENT_TYPE_REPAINT = 7;
constexpr int32_t TEXT_ANCHOR_MIDDLE_CENTER = 4;

inline int32_t currentEventType() {
  ManagedObject event = mainFunction<ManagedObject (*)(const void*)>(EVENT_GETCURRENT)(nullptr);
  if (event == nullptr) return -1;
  return mainFunction<int32_t (*)(ManagedObject, const void*)>(EVENT_GETTYPE)(event, nullptr);
}

inline ManagedObject whiteTexture() {
  return mainFunction<ManagedObject (*)(const void*)>(TEX2D_GETWHITE)(nullptr);
}

inline void drawTexture(Rect rect, ManagedObject texture) {
  mainFunction<void (*)(Rect, ManagedObject, const void*)>(GUI_DRAWTEXTURE)(rect, texture, nullptr);
}

inline void label(Rect rect, ManagedString text, ManagedObject style) {
  mainFunction<void (*)(Rect, ManagedString, ManagedObject, const void*)>(GUI_LABEL)(rect, text, style,
                                                                                    nullptr);
}

inline Color guiColor() { return mainFunction<Color (*)(const void*)>(GUI_GETCOLOR)(nullptr); }
inline void setGuiColor(Color c) { mainFunction<void (*)(Color, const void*)>(GUI_SETCOLOR)(c, nullptr); }
inline Color guiContentColor() { return mainFunction<Color (*)(const void*)>(GUI_GETCONTENTCOLOR)(nullptr); }
inline void setGuiContentColor(Color c) {
  mainFunction<void (*)(Color, const void*)>(GUI_SETCONTENTCOLOR)(c, nullptr);
}

/// GUI.skin.label —— 全局共享的样式，改了必须在同一帧内还原
inline ManagedObject labelStyle() {
  ManagedObject skin = mainFunction<ManagedObject (*)(const void*)>(GUI_GETSKIN)(nullptr);
  return mainFunction<ManagedObject (*)(ManagedObject, const void*)>(GUISKIN_GETLABEL)(skin, nullptr);
}

inline int32_t styleFontSize(ManagedObject style) {
  return mainFunction<int32_t (*)(ManagedObject, const void*)>(GUISTYLE_GETFONTSIZE)(style, nullptr);
}
inline void setStyleFontSize(ManagedObject style, int32_t size) {
  mainFunction<void (*)(ManagedObject, int32_t, const void*)>(GUISTYLE_SETFONTSIZE)(style, size, nullptr);
}
inline int32_t styleAlignment(ManagedObject style) {
  return mainFunction<int32_t (*)(ManagedObject, const void*)>(GUISTYLE_GETALIGNMENT)(style, nullptr);
}
inline void setStyleAlignment(ManagedObject style, int32_t anchor) {
  mainFunction<void (*)(ManagedObject, int32_t, const void*)>(GUISTYLE_SETALIGNMENT)(style, anchor, nullptr);
}

// ---------------------------------------------------------------- 字符串

/// ASCII C 串 → 托管字符串（Marshal.PtrToStringAnsi）
inline ManagedString asciiString(const char* text) {
  return mainFunction<ManagedString (*)(const char*, const void*)>(MARSHAL_PTRTOSTRANSI)(text, nullptr);
}

inline ManagedString intToString(int32_t value) {
  return mainFunction<ManagedString (*)(const int32_t*, const void*)>(INT32_TOSTRING)(&value, nullptr);
}

inline ManagedString charToString(char16_t value) {
  return mainFunction<ManagedString (*)(const char16_t*, const void*)>(CHAR_TOSTRING)(&value, nullptr);
}

inline ManagedString concat(ManagedString a, ManagedString b, ManagedString c) {
  return mainFunction<ManagedString (*)(ManagedString, ManagedString, ManagedString, const void*)>(
      STRING_CONCAT3)(a, b, c, nullptr);
}

/// LocalisedString 是值类型，传它在宿主对象里的地址
inline ManagedString localisedToString(const void* localisedString) {
  return mainFunction<ManagedString (*)(const void*, const void*)>(LS_TOSTRING)(localisedString, nullptr);
}

// ---------------------------------------------------------------- PlayMaker

/// 不存在时会自建 GameObject 并挂 PlayMakerGUI（反汇编确认），从而保证每帧有 OnGUI
inline ManagedObject ensurePlayMakerGui() {
  return mainFunction<ManagedObject (*)(const void*)>(PMGUI_GETINSTANCE)(nullptr);
}

}  // namespace sshud::unity
