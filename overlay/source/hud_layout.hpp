// 血条布局与配色 —— 纯计算，不碰 libtesla，可在宿主机上测试。
//
// 坐标系：libtesla 的 framebuffer 像素与 1280x720 游戏屏幕空间 1:1 对应
// （已由研究确认：LayerWidth = 1920 * FBW/1280，恒定 1.5x 上采样）。
// 所以这里算出来的像素坐标可以直接喂给 drawRect。

#ifndef SS_HUD_LAYOUT_HPP
#define SS_HUD_LAYOUT_HPP

#include <stdint.h>

#include "../../shared/hud_protocol.h"
#include "hud_reader.hpp"

namespace sshud {

// --- v1 的 framebuffer 尺寸 ---
//
// 宽度必须是 32 的倍数（libnx 把 stride 对齐到 64 字节，而 libtesla 的
// swizzle 假设 stride == FramebufferWidth）。
// 高度 256：双缓冲占 1280*256*2*2 = 1.3 MB，对 nx-ovlloader 的 6 MB
// applet 堆是安全余量；全屏 720 需要 3.6 MB，太紧。
constexpr int FB_WIDTH = 1280;
constexpr int FB_HEIGHT = 256;

static_assert(FB_WIDTH % 32 == 0, "framebuffer 宽度必须是 32 的倍数");

// --- 条的几何 ---
constexpr int BAR_MARGIN_X = 40;
constexpr int BOSS_BAR_Y = 24;
constexpr int BOSS_BAR_H = 18;
constexpr int BOSS_BAR_W = FB_WIDTH - 2 * BAR_MARGIN_X;

constexpr int MOB_BAR_X = FB_WIDTH - 340;
constexpr int MOB_BAR_TOP = 72;
constexpr int MOB_BAR_W = 280;
constexpr int MOB_BAR_H = 12;
constexpr int MOB_BAR_GAP = 22;

constexpr int BAR_BORDER = 2;

/// 淡出：受击后前 SECONDS_SOLID 秒全不透明，之后线性淡到 0。
constexpr double FADE_SOLID_SECONDS = 2.5;
constexpr double FADE_OUT_SECONDS = 1.5;

/// libtesla 的 Color 是 RGBA4444，每通道 0..15。
struct BarColor {
  uint8_t r, g, b, a;
};

/// 一条血条的完整绘制参数。
struct BarLayout {
  bool visible;
  int x, y, w, h;
  int fillWidth;     ///< 填充部分的宽度（已按血量比例算好并夹取）
  BarColor fill;
  BarColor background;
  BarColor border;
  uint8_t alpha;     ///< 整体透明度 0..15，由淡出计算得出
  int32_t hp, maxHp, lastDamage;
  bool isBoss;
};

/// 按剩余血量比例选填充色：绿 → 黄 → 红。
///
/// 阈值取 0.5 / 0.25，和 PC 端血条 mod 的观感一致。
inline BarColor fillColorFor(float fraction) {
  if (fraction > 0.5f) return BarColor{0x4, 0xD, 0x5, 0xF};   // 绿
  if (fraction > 0.25f) return BarColor{0xE, 0xC, 0x3, 0xF};  // 黄
  return BarColor{0xE, 0x3, 0x3, 0xF};                        // 红
}

/// 受击后经过 elapsed 秒时的整体透明度（0..15）。
inline uint8_t fadeAlphaFor(double elapsedSeconds) {
  if (elapsedSeconds < 0.0) return 0xF;
  if (elapsedSeconds <= FADE_SOLID_SECONDS) return 0xF;
  const double t = (elapsedSeconds - FADE_SOLID_SECONDS) / FADE_OUT_SECONDS;
  if (t >= 1.0) return 0;
  return (uint8_t)(15.0 * (1.0 - t) + 0.5);
}

/// 计算第 slot 条普通敌人血条的 Y 坐标。
inline int mobBarY(int slot) { return MOB_BAR_TOP + slot * MOB_BAR_GAP; }

/// 这一条是否还放得进 framebuffer。
inline bool mobSlotFits(int slot) {
  return mobBarY(slot) + MOB_BAR_H + BAR_BORDER <= FB_HEIGHT;
}

/// 为单个敌人算出绘制参数。
///
/// @param bossSlot  Boss 的序号（0 表示第一个 Boss）；非 Boss 传 -1
/// @param mobSlot   普通敌人的序号；Boss 传 -1
inline BarLayout layoutFor(const SsHudBlock& block, const SsHudEntity& entity,
                           int bossSlot, int mobSlot) {
  BarLayout out = {};
  out.hp = entity.hp;
  out.maxHp = entity.maxHp;
  out.lastDamage = entity.lastDamage;
  out.isBoss = (entity.flags & SSHUD_FLAG_BOSS) != 0;

  const double elapsed = secondsSinceHit(block, entity);
  out.alpha = fadeAlphaFor(elapsed);
  if (out.alpha == 0) {
    out.visible = false;
    return out;
  }

  if (out.isBoss) {
    if (bossSlot < 0) {
      out.visible = false;
      return out;
    }
    out.x = BAR_MARGIN_X;
    out.y = BOSS_BAR_Y + bossSlot * (BOSS_BAR_H + 10);
    out.w = BOSS_BAR_W;
    out.h = BOSS_BAR_H;
  } else {
    if (mobSlot < 0 || !mobSlotFits(mobSlot)) {
      out.visible = false;
      return out;
    }
    out.x = MOB_BAR_X;
    out.y = mobBarY(mobSlot);
    out.w = MOB_BAR_W;
    out.h = MOB_BAR_H;
  }

  const float fraction = hpFraction(entity);
  out.fillWidth = (int)(fraction * (float)out.w + 0.5f);
  if (out.fillWidth < 0) out.fillWidth = 0;
  if (out.fillWidth > out.w) out.fillWidth = out.w;

  out.fill = fillColorFor(fraction);
  out.background = BarColor{0x1, 0x1, 0x1, 0xC};
  out.border = BarColor{0x0, 0x0, 0x0, 0xF};
  out.visible = true;
  return out;
}

/// 把一份快照排布成若干条血条。
///
/// Boss 排顶部通栏，普通敌人排右侧竖列，各自按最近受击时间从新到旧。
/// 返回实际产生的条数。
inline int layoutSnapshot(const SsHudBlock& block, BarLayout* out, int outCapacity) {
  if (out == nullptr || outCapacity <= 0) return 0;

  // 按最近受击时间降序取索引。条目最多 8 个，插入排序足够。
  int order[SSHUD_MAX_ENTITIES];
  int count = 0;
  const uint32_t n =
      block.entityCount > SSHUD_MAX_ENTITIES ? SSHUD_MAX_ENTITIES : block.entityCount;
  for (uint32_t i = 0; i < n; ++i) {
    int pos = count;
    while (pos > 0 &&
           block.entities[order[pos - 1]].lastHitTick < block.entities[i].lastHitTick) {
      order[pos] = order[pos - 1];
      --pos;
    }
    order[pos] = (int)i;
    ++count;
  }

  int produced = 0, bossSlot = 0, mobSlot = 0;
  for (int i = 0; i < count && produced < outCapacity; ++i) {
    const SsHudEntity& entity = block.entities[order[i]];
    const bool isBoss = (entity.flags & SSHUD_FLAG_BOSS) != 0;
    const BarLayout layout =
        layoutFor(block, entity, isBoss ? bossSlot : -1, isBoss ? -1 : mobSlot);
    if (!layout.visible) continue;
    out[produced++] = layout;
    if (isBoss) {
      ++bossSlot;
    } else {
      ++mobSlot;
    }
  }
  return produced;
}

}  // namespace sshud

#endif  // SS_HUD_LAYOUT_HPP
