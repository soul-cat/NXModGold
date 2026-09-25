// 开关面板的中文文字：IMGUI 默认字体在 Switch 上没有中文字形，所以文字离线渲染成白色贴图
// （tools/61-render-menu-text.py → exl/assets/menu/*.png → bar_textures.gen.h），运行时按颜色染色画出。
// 所有文字贴图等高，按统一高度缩放即可对齐。

#pragma once

#include "bar_textures.hpp"
#include "toggles_model.hpp"

namespace sshud::hud {

/// 一张文字贴图及其源像素尺寸
struct TextImage {
  unity::ManagedObject texture = nullptr;
  float width = 0.0f;
  unity::PinHandle pin = 0;
};

struct MenuTextures {
  TextImage title;
  TextImage rows[toggles::PANEL_FEATURE_COUNT];  // 顺序同 toggles::FEATURES
  TextImage on;
  TextImage off;
};

inline void ensureTextImage(TextImage& image, const TextureBlob& blob) {
  ensureTexture(image.texture, image.pin, blob);
  image.width = static_cast<float>(blob.width);
}

inline bool isUsable(const MenuTextures& textures) {
  for (const TextImage& row : textures.rows) {
    if (!unity::isAlive(row.texture)) return false;
  }
  return unity::isAlive(textures.title.texture) && unity::isAlive(textures.on.texture) &&
         unity::isAlive(textures.off.texture);
}

static_assert(toggles::PANEL_FEATURE_COUNT == 4, "menuTextures() 按 FEATURES 顺序逐行建贴图，增删功能要同步改");

/// 第一次用时建，被卸掉了就只重建失效的；失败后按 RetryThrottle 节流（now：unscaledTime）
inline const MenuTextures* menuTextures(float now) {
  static MenuTextures s_textures;
  static RetryThrottle s_retry;
  if (isUsable(s_textures)) return &s_textures;
  if (!s_retry.tryAcquire(now)) return nullptr;
  ensureTextImage(s_textures.title, MENU_TEXT_TITLE);
  ensureTextImage(s_textures.rows[0], MENU_TEXT_ENEMY_BARS);
  ensureTextImage(s_textures.rows[1], MENU_TEXT_BOSS_BAR);
  ensureTextImage(s_textures.rows[2], MENU_TEXT_DAMAGE_NUMBERS);
  ensureTextImage(s_textures.rows[3], MENU_TEXT_MAP_MARKERS);
  ensureTextImage(s_textures.on, MENU_TEXT_ON);
  ensureTextImage(s_textures.off, MENU_TEXT_OFF);
  return isUsable(s_textures) ? &s_textures : nullptr;
}

}  // namespace sshud::hud
