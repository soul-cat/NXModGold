// 从原生代码直接调用 IL2CPP 编译后的 Unity 方法。
//
// IL2CPP 方法的原生签名 = C# 参数 + 末尾一个 const MethodInfo*；实例方法首参是 this。
// 非泛型方法不读 MethodInfo*，传 nullptr 即可。
// Vector3 是 3 个 float 的同质聚合（HFA），AAPCS64 下经 s0–s2 传参/返回，
// 与下面的 C++ 结构体完全一致，所以可以按值传。
//
// 只能在 Unity 主线程上调用（TakeDamage、LateUpdate、OnGUI 都在主线程）。

#pragma once

#include "lib.hpp"
#include "offsets.h"

#include <cstdint>

namespace sshud::unity {

struct Vector3 {
  float x;
  float y;
  float z;
};

using GetTransformFn = void* (*)(const void* component, const void* method);
using GetPositionFn = Vector3 (*)(void* transform, const void* method);
using GetMainCameraFn = void* (*)(const void* method);
using WorldToScreenFn = Vector3 (*)(void* camera, Vector3 position, const void* method);
using GetPixelSizeFn = int32_t (*)(void* camera, const void* method);

template <typename Fn>
Fn mainFunction(uintptr_t rva) {
  return reinterpret_cast<Fn>(exl::util::modules::GetTargetOffset(rva));
}

/// component.transform.position
inline Vector3 worldPositionOf(const void* component) {
  void* transform = mainFunction<GetTransformFn>(COMP_GETTRANSFORM)(component, nullptr);
  return mainFunction<GetPositionFn>(TR_GETPOSITION)(transform, nullptr);
}

/// Camera.main；场景切换中可能为 null
inline void* mainCamera() {
  return mainFunction<GetMainCameraFn>(CAM_GETMAIN)(nullptr);
}

/// 像素坐标，原点在左下；z 为到相机的距离（< 0 表示在相机背后）
inline Vector3 worldToScreen(void* camera, Vector3 world) {
  return mainFunction<WorldToScreenFn>(CAM_WORLDTOSCREEN)(camera, world, nullptr);
}

inline int32_t pixelWidth(void* camera) {
  return mainFunction<GetPixelSizeFn>(CAM_PIXELWIDTH)(camera, nullptr);
}

inline int32_t pixelHeight(void* camera) {
  return mainFunction<GetPixelSizeFn>(CAM_PIXELHEIGHT)(camera, nullptr);
}

}  // namespace sshud::unity
