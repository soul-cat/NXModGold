// 地图功能用到的托管调用薄封装（IL2CPP 原生调用：参数后追加 MethodInfo* = null）。

#pragma once

#include "lib.hpp"
#include "unity_api.hpp"

namespace sshud::map {

namespace ui = sshud::unity;

constexpr uintptr_t IL2CPP_ARRAY_LENGTH = 0x18;
constexpr uintptr_t IL2CPP_ARRAY_ITEMS = 0x20;

struct Vector2 {
  float x;
  float y;
};

template <typename R, typename... A>
R call(uintptr_t rva, A... args) {
  return ui::mainFunction<R (*)(A..., const void*)>(rva)(args..., nullptr);
}

inline ui::ManagedString managedString(const char* text) { return call<ui::ManagedString>(MARSHAL_PTRTOSTRANSI, text); }

inline uint64_t arrayLength(const void* array) { return ui::readField<uint64_t>(array, IL2CPP_ARRAY_LENGTH); }
inline ui::ManagedObject arrayItem(const void* array, uint64_t i) {
  return ui::readField<ui::ManagedObject>(array, IL2CPP_ARRAY_ITEMS + i * sizeof(void*));
}

inline ui::ManagedObject transformOf(ui::ManagedObject go) { return call<ui::ManagedObject>(GO_GETTRANSFORM, go); }
inline ui::ManagedObject gameObjectOf(ui::ManagedObject c) { return call<ui::ManagedObject>(COMP_GETGAMEOBJECT, c); }
inline void setActive(ui::ManagedObject go, bool on) { call<void>(GO_SETACTIVE, go, on); }
inline void destroyObject(ui::ManagedObject o) { call<void>(UOBJ_DESTROY, o); }

inline ui::ManagedObject findChild(ui::ManagedObject tr, const char* name) {
  return call<ui::ManagedObject>(TR_FIND, tr, managedString(name));
}
inline ui::ManagedObject childAt(ui::ManagedObject tr, int32_t i) { return call<ui::ManagedObject>(TR_GETCHILD, tr, i); }
inline int32_t childCount(ui::ManagedObject tr) { return call<int32_t>(TR_GETCHILDCOUNT, tr); }
inline int32_t siblingIndex(ui::ManagedObject tr) { return call<int32_t>(TR_GETSIBLINGINDEX, tr); }
inline ui::ManagedObject componentByName(ui::ManagedObject go, const char* type) {
  return call<ui::ManagedObject>(GO_GETCOMPONENT_STR, go, managedString(type));
}

}  // namespace sshud::map
