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
};

struct MenuTextures {
  TextImage title;
  TextImage rows[toggles::FEATURE_COUNT];  // 顺序同 toggles::FEATURES
  TextImage on;
  TextImage off;
};

inline TextImage createTextImage(const TextureBlob& blob) {
  return {createTexture(blob), static_cast<float>(blob.width)};
}

inline bool isUsable(const MenuTextures& textures) {
  for (const TextImage& row : textures.rows) {
    if (!unity::isAlive(row.texture)) return false;
  }
  return unity::isAlive(textures.title.texture) && unity::isAlive(textures.on.texture) &&
         unity::isAlive(textures.off.texture);
}

static_assert(toggles::FEATURE_COUNT == 4, "menuTextures() 按 FEATURES 顺序逐行建贴图，增删功能要同步改");

/// 第一次用时建，被卸掉了就重建
inline const MenuTextures* menuTextures() {
  static MenuTextures s_textures;
  if (!isUsable(s_textures)) {
    s_textures.title = createTextImage(MENU_TEXT_TITLE);
    s_textures.rows[0] = createTextImage(MENU_TEXT_ENEMY_BARS);
    s_textures.rows[1] = createTextImage(MENU_TEXT_BOSS_BAR);
    s_textures.rows[2] = createTextImage(MENU_TEXT_DAMAGE_NUMBERS);
    s_textures.rows[3] = createTextImage(MENU_TEXT_MAP_MARKERS);
    s_textures.on = createTextImage(MENU_TEXT_ON);
    s_textures.off = createTextImage(MENU_TEXT_OFF);
  }
  return isUsable(s_textures) ? &s_textures : nullptr;
}

}  // namespace sshud::hud
