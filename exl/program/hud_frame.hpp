// HUD 一帧的 IMGUI 上下文与基础绘制：保存/还原全局 IMGUI 状态、屏幕与相机参数、世界 → 屏幕投影、画一行字。
// （从 hud_draw.hpp 拆出，hud_draw.hpp 只管画什么。）

#pragma once

#include "hud_font.hpp"
#include "hud_model.hpp"
#include "unity_api.hpp"

#include <algorithm>
#include <cmath>

namespace sshud::hud {

namespace ui = sshud::unity;

constexpr float TEXT_BOX_WIDTH_FACTOR = 6.0f;  // 文字框宽度 = 字号 × 此值，足够放下 "9999/9999"

/// 保存并在析构时还原本模块会改动的 IMGUI 全局状态（含默认字体：换成游戏字体，见 hud_font.hpp）
class ImguiStateGuard {
 public:
  ImguiStateGuard()
      : style_(ui::labelStyle()),
        color_(ui::guiColor()),
        contentColor_(ui::guiContentColor()),
        fontSize_(ui::styleFontSize(style_)),
        alignment_(ui::styleAlignment(style_)),
        font_(defaultFont()) {
    ui::setStyleAlignment(style_, ui::TEXT_ANCHOR_MIDDLE_CENTER);
    ui::ManagedObject gameFont = findGameFont();
    logGameFont(gameFont);
    if (gameFont != nullptr) setDefaultFont(gameFont);
  }
  ~ImguiStateGuard() {
    setDefaultFont(font_);
    ui::setGuiColor(color_);
    ui::setGuiContentColor(contentColor_);
    ui::setStyleFontSize(style_, fontSize_);
    ui::setStyleAlignment(style_, alignment_);
  }
  ImguiStateGuard(const ImguiStateGuard&) = delete;
  ImguiStateGuard& operator=(const ImguiStateGuard&) = delete;

  ui::ManagedObject style() const { return style_; }

 private:
  ui::ManagedObject style_;
  ui::Color color_;
  ui::Color contentColor_;
  int32_t fontSize_;
  int32_t alignment_;
  ui::ManagedObject font_;
};

/// 一帧的绘制上下文
struct Frame {
  float now;
  float width;
  float height;
  ui::ManagedObject camera;  // 可能为 null（场景切换中）
  float cameraScaleX;        // 相机像素 → 屏幕像素
  float cameraScaleY;
  ui::ManagedObject white;
  ui::ManagedObject style;
};

inline ui::Rect toUi(const Rect& r) { return {r.x, r.y, r.width, r.height}; }

/// 以 (centerX, centerY) 为中心、在 boxWidth 宽的框里画一行字；字号按 FONT_SIZE_STEP 取整，免得动态字体反复重建图集
inline void drawTextInWidth(const Frame& frame, float centerX, float centerY, float boxWidth, float fontSize,
                            ui::ManagedString text, ui::Color color) {
  if (text == nullptr) return;
  const int32_t fontPixels = quantizeFontSize(fontSize);
  const float boxHeight = static_cast<float>(fontPixels) * 2.0f;
  ui::setStyleFontSize(frame.style, fontPixels);
  ui::setGuiContentColor(color);
  ui::label({centerX - boxWidth * 0.5f, centerY - boxHeight * 0.5f, boxWidth, boxHeight}, text, frame.style);
}

/// 短文字（数字、BOSS 名）：框宽 = 字号 × TEXT_BOX_WIDTH_FACTOR
inline void drawText(const Frame& frame, float centerX, float centerY, float fontSize,
                     ui::ManagedString text, ui::Color color) {
  const float boxWidth = static_cast<float>(quantizeFontSize(fontSize)) * TEXT_BOX_WIDTH_FACTOR;
  drawTextInWidth(frame, centerX, centerY, boxWidth, fontSize, text, color);
}

/// 世界坐标 → IMGUI 坐标（原点左上）；在相机背后返回 false
inline bool project(const Frame& frame, ui::Vector3 world, float& outX, float& outY) {
  const ui::Vector3 screen = ui::worldToScreen(frame.camera, world);
  if (screen.z < 0.0f) return false;
  outX = screen.x * frame.cameraScaleX;
  outY = frame.height - screen.y * frame.cameraScaleY;
  return true;
}

/// 该世界位置附近一个世界单位对应的屏幕像素数
inline float pixelsPerUnit(const Frame& frame, ui::Vector3 world, float screenX) {
  float rightX = 0.0f;
  float rightY = 0.0f;
  if (!project(frame, {world.x + 1.0f, world.y, world.z}, rightX, rightY)) return 0.0f;
  return std::fabs(rightX - screenX);
}

/// now：本帧 unscaledTime（调用方已取过，不再重复调用）
inline Frame beginFrame(ui::ManagedObject style, float now) {
  Frame frame{now, static_cast<float>(ui::screenWidth()), static_cast<float>(ui::screenHeight()),
              ui::mainCamera(), 1.0f, 1.0f, ui::whiteTexture(), style};
  if (ui::isAlive(frame.camera)) {
    frame.cameraScaleX = frame.width / static_cast<float>(std::max(1, ui::cameraPixelWidth(frame.camera)));
    frame.cameraScaleY = frame.height / static_cast<float>(std::max(1, ui::cameraPixelHeight(frame.camera)));
  } else {
    frame.camera = nullptr;
  }
  return frame;
}

}  // namespace sshud::hud
