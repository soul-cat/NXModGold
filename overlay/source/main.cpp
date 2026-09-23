// Silksong 血条 HUD —— Tesla overlay（常驻层 + 输入穿透）。
//
// 常驻的做法来自 Status-Monitor-Overlay 的 Mini 模式：
//   FullMode = false                    去掉全屏边框
//   TeslaFPS = <刷新率>                 接管帧节奏（禁用内建 60Hz 限速）
//   deactivateOriginalFooter = true     去掉 "Back / OK" 页脚
//   alphabackground = 0x0               fillScreen 变成透明清屏
//   tsl::hlp::requestForeground(false)  把输入还给游戏
//
// 关键：**永远不要调用 hide()**。层一旦隐藏就只剩透明帧。
//
// ⚠️ 上游 WerWolv/libtesla 做不到这件事（framebuffer 尺寸写死 448x720、
//    没有 setLayerPos()、OverlayFrame::draw 无条件画黑底和页脚）。
//    必须用 ppkantorski/libultrahand 或 Status-Monitor 的改造版。
//
// ⚠️ 输入是【复制】不是移交 —— overlay 进程是 AppletType_None，直接读
//    HID 共享内存，所以游戏和 overlay 会同时收到同一次按键。
//    退出组合键必须避开小骑士的操作键。

// 这个 tesla fork 是"头文件即实现"：主实现段（含 tsl::cfg 全局、
// __libnx_* 定义、主循环）在 TESLA_INIT_IMPL 宏后面，整个项目必须
// 恰好在一个 .cpp 里带宏 include tesla.hpp（本文件即是）。
#define TESLA_INIT_IMPL
#include <tesla.hpp>
extern "C" {
#include "saltysd_ipc_sm.h"   // Status-Monitor-Overlay 的 SaltyNX 客户端（Connect/Term/共享内存全套）
}

#include <cstring>

#include "../../shared/hud_protocol.h"
#include "hud_layout.hpp"
#include "hud_reader.hpp"
#include "saltynx_client.hpp"

namespace {

/// 刷新率。Status-Monitor 的文档反复强调刷新率越高 core3 占用越高，
/// 各模式默认给到 1。但血条要跟手，1Hz 完全不能看。
/// 20Hz 是权衡值；endFrame() 仍无条件 waitForVSync()，所以 60 是硬上限。
constexpr uint8_t REFRESH_HZ = 20;

/// 退出组合键。必须选游戏用不到的组合 —— 因为按键会同时传进游戏。
/// 小骑士用到 A/B/X/Y/ZL/ZR/L/R/摇杆，右摇杆按下（RS）游戏里没用。
constexpr uint64_t EXIT_COMBO = HidNpadButton_L | HidNpadButton_R | HidNpadButton_StickR;

SharedMemory g_shmem = {};
bool g_shmemMapped = false;
sshud::HudReader g_reader;

// ---------------------------------------------------------------------------
// SaltyNX 连接
// ---------------------------------------------------------------------------

/// ping "InjectServ" 确认 SaltyNX 活着且没卡住。
/// 两轮循环是参考实现的做法 —— 第一轮可能撞上还在初始化的窗口。
bool checkSaltyNxAlive() {
  Handle port = 0;
  for (int round = 0; round < 2; ++round) {
    bool ok = false;
    for (int i = 0; i < 67; ++i) {
      if (R_SUCCEEDED(svcConnectToNamedPort(&port, "InjectServ"))) {
        svcCloseHandle(port);
        ok = true;
        break;
      }
      svcSleepThread(1'000'000);
    }
    if (!ok) return false;
  }
  return true;
}

/// 连接、取句柄、关会话、映射。注意先 Term 再 map —— 句柄在会话关闭后仍有效。
bool mapSharedMemory() {
  if (g_shmemMapped) return true;
  if (!checkSaltyNxAlive()) return false;

  Handle remote = 0;
  if (R_FAILED(SaltySD_Connect())) return false;
  if (R_FAILED(SaltySD_GetSharedMemoryHandle(&remote))) {
    SaltySD_Term();
    return false;
  }
  SaltySD_Term();

  shmemLoadRemote(&g_shmem, remote, SSHUD_SHMEM_TOTAL, Perm_Rw);
  if (R_FAILED(shmemMap(&g_shmem))) return false;

  g_shmemMapped = true;
  return true;
}

/// 在共享内存里定位我们的块并接上读取端。
/// 插件可能比 overlay 晚起来，所以这一步每帧重试是合理的。
bool attachBlock() {
  if (!g_shmemMapped) return false;
  if (g_reader.isAttached()) return true;

  const auto* base = static_cast<const uint8_t*>(shmemGetAddr(&g_shmem));
  const int32_t offset =
      sshud::findBlockOffset(base, SSHUD_SHMEM_TOTAL, SSHUD_MAGIC);
  if (offset < 0) return false;

  g_reader.attach(reinterpret_cast<const SsHudBlock*>(base + offset));
  return true;
}

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------

inline tsl::gfx::Color toTeslaColor(const sshud::BarColor& c, uint8_t alpha) {
  // alpha 取条自身的 a 与整体淡出值中较小者
  const uint8_t a = c.a < alpha ? c.a : alpha;
  return tsl::gfx::Color(c.r, c.g, c.b, a);
}

void drawBar(tsl::gfx::Renderer* renderer, const sshud::BarLayout& bar) {
  const int bd = sshud::BAR_BORDER;

  // 边框 → 底色 → 填充，三层叠上去
  renderer->drawRect(bar.x - bd, bar.y - bd, bar.w + 2 * bd, bar.h + 2 * bd,
                     renderer->a(toTeslaColor(bar.border, bar.alpha)));
  renderer->drawRect(bar.x, bar.y, bar.w, bar.h,
                     renderer->a(toTeslaColor(bar.background, bar.alpha)));
  if (bar.fillWidth > 0) {
    renderer->drawRect(bar.x, bar.y, bar.fillWidth, bar.h,
                       renderer->a(toTeslaColor(bar.fill, bar.alpha)));
  }

  // 数值文本：Boss 放条内左侧，普通敌人放条左边
  char text[32];
  std::snprintf(text, sizeof(text), "%d/%d", bar.hp, bar.maxHp);
  const tsl::gfx::Color textColor(0xF, 0xF, 0xF, bar.alpha);

  if (bar.isBoss) {
    renderer->drawString(text, false, bar.x + 8, bar.y + bar.h - 4, 14,
                         renderer->a(textColor));
    if (bar.lastDamage > 0) {
      char dmg[16];
      std::snprintf(dmg, sizeof(dmg), "-%d", bar.lastDamage);
      renderer->drawString(dmg, false, bar.x + bar.w - 48, bar.y + bar.h - 4, 14,
                           renderer->a(tsl::gfx::Color(0xF, 0xC, 0x4, bar.alpha)));
    }
  } else {
    renderer->drawString(text, false, bar.x - 78, bar.y + bar.h, 12,
                         renderer->a(textColor));
    if (bar.lastDamage > 0) {
      char dmg[16];
      std::snprintf(dmg, sizeof(dmg), "-%d", bar.lastDamage);
      renderer->drawString(dmg, false, bar.x + bar.w + 6, bar.y + bar.h, 12,
                           renderer->a(tsl::gfx::Color(0xF, 0xC, 0x4, bar.alpha)));
    }
  }
}

/// 插件没起来或版本不符时，画一行诊断而不是干等着。
void drawDiagnostic(tsl::gfx::Renderer* renderer, const char* message) {
  renderer->drawString(message, false, sshud::BAR_MARGIN_X, 40, 18,
                       renderer->a(tsl::gfx::Color(0xF, 0x8, 0x8, 0xF)));
}

// ---------------------------------------------------------------------------
class HudGui : public tsl::Gui {
 public:
  HudGui() {
    // 常驻模式配方 —— 缺任何一项都会变回普通菜单
    FullMode = false;
    TeslaFPS = REFRESH_HZ;
    deactivateOriginalFooter = true;
    alphabackground = 0x0;
    tsl::hlp::requestForeground(false);
  }

  ~HudGui() override {
    // 退出时还原，否则会影响后续打开的其他 overlay
    FullMode = true;
    TeslaFPS = 0;
    deactivateOriginalFooter = false;
    alphabackground = 0xD;
    tsl::hlp::requestForeground(true);
  }

  tsl::elm::Element* createUI() override {
    auto* frame = new tsl::elm::OverlayFrame("", "");
    frame->setContent(new tsl::elm::CustomDrawer(
        [this](tsl::gfx::Renderer* renderer, u16, u16, u16, u16) { this->render(renderer); }));
    return frame;
  }

  bool handleInput(u64 keysDown, u64, const HidTouchState&, HidAnalogStickState,
                   HidAnalogStickState) override {
    if ((keysDown & EXIT_COMBO) == EXIT_COMBO) {
      TeslaFPS = 0;
      tsl::goBack();
      return true;
    }
    return false;
  }

 private:
  void render(tsl::gfx::Renderer* renderer) {
    if (!attachBlock()) {
      drawDiagnostic(renderer, "等待插件…");
      return;
    }

    SsHudBlock snapshot;
    const sshud::ReadStatus status = g_reader.snapshot(&snapshot);
    if (status != sshud::ReadStatus::Ok) {
      // Torn 是瞬时的，不值得报给用户；其他状态要显示出来
      if (status != sshud::ReadStatus::Torn) {
        drawDiagnostic(renderer, sshud::HudReader::statusText(status));
      }
      return;
    }

    if (const char* diag = sshud::HudReader::hookDiagnosis(snapshot.hookStatus)) {
      drawDiagnostic(renderer, diag);
      return;
    }

    sshud::BarLayout bars[SSHUD_MAX_ENTITIES];
    const int count = sshud::layoutSnapshot(snapshot, bars, SSHUD_MAX_ENTITIES);
    for (int i = 0; i < count; ++i) drawBar(renderer, bars[i]);
  }
};

class HudOverlay : public tsl::Overlay {
 public:
  void initServices() override {
    mapSharedMemory();  // 失败不致命，render 里会重试并显示诊断
  }

  void exitServices() override {
    if (g_shmemMapped) {
      shmemClose(&g_shmem);
      g_shmemMapped = false;
    }
  }

  std::unique_ptr<tsl::Gui> loadInitialGui() override {
    return initially<HudGui>();
  }
};

}  // namespace

int main(int argc, char** argv) {
  // framebuffer 尺寸只在 Renderer::init() 读一次，必须在 tsl::loop 之前设好，
  // 运行时改不了（只有 setLayerPos 可以运行时调）。
  framebufferWidth = sshud::FB_WIDTH;
  framebufferHeight = sshud::FB_HEIGHT;
  return tsl::loop<HudOverlay>(argc, argv);
}
