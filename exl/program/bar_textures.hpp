// BOSS 条贴图：作者自绘的白色贴图（exl/assets/*.png），tools/60-gen-bar-textures.py 切成三段编进模块，
// 运行时建成 Texture2D，用 GUI.color 染色、三宫格拉伸（两端尖角不变形）。
//
// 建贴图：il2cpp_codegen_object_new(Texture2D) → .ctor(w, h, RGBA32, false) → LoadRawTextureData(ptr) → Apply。
// 贴图跨帧使用，所以：GCHandle.Alloc 钉住托管包装（本模块的静态区 GC 扫不到）；
// hideFlags = DontUnloadUnusedAsset，免得切场景时 Resources.UnloadUnusedAssets 把它卸掉。

#pragma once

#include "bar_textures.gen.h"
#include "hud_model.hpp"
#include "lib.hpp"
#include "unity_api.hpp"

namespace sshud::hud {

constexpr int32_t TEXTURE_FORMAT_RGBA32 = 4;
constexpr int32_t TEXTURE_WRAP_CLAMP = 1;
constexpr int32_t HIDE_FLAGS_DONT_UNLOAD_UNUSED_ASSET = 32;
constexpr int32_t RGBA_BYTES = 4;

/// 三宫格贴图：两端按原比例、中段横向拉伸
struct SlicedTexture {
  unity::ManagedObject left = nullptr;
  unity::ManagedObject middle = nullptr;
  unity::ManagedObject right = nullptr;
  float leftWidth = 0.0f;   // 源像素
  float rightWidth = 0.0f;
  float sourceHeight = 1.0f;
};

inline const void* texture2DClass() {
  auto* slot = reinterpret_cast<const void* const*>(exl::util::modules::GetTargetOffset(TEX2D_TYPEINFO));
  if (*slot == nullptr) unity::mainFunction<void (*)(const void*, bool)>(IL2CPP_INIT_METADATA)(slot, true);
  return *slot;
}

inline unity::ManagedObject createTexture(const TextureBlob& blob) {
  const void* klass = texture2DClass();
  if (klass == nullptr) return nullptr;
  void* texture = unity::mainFunction<void* (*)(const void*)>(IL2CPP_OBJECT_NEW)(klass);
  if (texture == nullptr) return nullptr;
  unity::mainFunction<void (*)(void*, int32_t, int32_t, int32_t, bool, const void*)>(TEX2D_CTOR)(
      texture, blob.width, blob.height, TEXTURE_FORMAT_RGBA32, false, nullptr);
  unity::mainFunction<void (*)(void*, const void*, int32_t, const void*)>(TEX2D_LOADRAWDATA)(
      texture, blob.rgba, blob.width * blob.height * RGBA_BYTES, nullptr);
  unity::mainFunction<void (*)(void*, const void*)>(TEX2D_APPLY)(texture, nullptr);
  unity::mainFunction<void (*)(void*, int32_t, const void*)>(TEX_SETWRAPMODE)(texture, TEXTURE_WRAP_CLAMP, nullptr);
  unity::mainFunction<void (*)(void*, int32_t, const void*)>(UOBJ_SETHIDEFLAGS)(
      texture, HIDE_FLAGS_DONT_UNLOAD_UNUSED_ASSET, nullptr);
  unity::mainFunction<uintptr_t (*)(void*, const void*)>(GCHANDLE_ALLOC)(texture, nullptr);
  return texture;
}

inline SlicedTexture createSliced(const TextureBlob& left, const TextureBlob& middle, const TextureBlob& right) {
  return {createTexture(left), createTexture(middle), createTexture(right), static_cast<float>(left.width),
          static_cast<float>(right.width), static_cast<float>(left.height)};
}

inline bool isUsable(const SlicedTexture& sliced) {
  return unity::isAlive(sliced.left) && unity::isAlive(sliced.middle) && unity::isAlive(sliced.right);
}

/// BOSS 条的外框与填充；第一次用时建，被卸掉了就重建
struct BossBarTextures {
  SlicedTexture frame;
  SlicedTexture fill;
};

inline const BossBarTextures* bossBarTextures() {
  static BossBarTextures s_textures;
  if (!isUsable(s_textures.frame) || !isUsable(s_textures.fill)) {
    s_textures.frame = createSliced(BOSS_FRAME_LEFT, BOSS_FRAME_MID, BOSS_FRAME_RIGHT);
    s_textures.fill = createSliced(BOSS_FILL_LEFT, BOSS_FILL_MID, BOSS_FILL_RIGHT);
  }
  return isUsable(s_textures.frame) && isUsable(s_textures.fill) ? &s_textures : nullptr;
}

/// 在 rect 里画三宫格：两端按高度等比，宽度不够时两端一起按比例缩
inline void drawSliced(const SlicedTexture& sliced, const Rect& rect, unity::Color tint) {
  if (rect.width <= 0.0f || rect.height <= 0.0f) return;
  const float scale = rect.height / sliced.sourceHeight;
  float left = sliced.leftWidth * scale;
  float right = sliced.rightWidth * scale;
  if (rect.width < left + right) {
    const float shrink = rect.width / (left + right);
    left *= shrink;
    right *= shrink;
  }
  unity::setGuiColor(tint);
  unity::drawTexture(unity::Rect{rect.x, rect.y, left, rect.height}, sliced.left);
  const float middle = rect.width - left - right;
  if (middle > 0.0f) unity::drawTexture(unity::Rect{rect.x + left, rect.y, middle, rect.height}, sliced.middle);
  unity::drawTexture(unity::Rect{rect.x + rect.width - right, rect.y, right, rect.height}, sliced.right);
}

}  // namespace sshud::hud
