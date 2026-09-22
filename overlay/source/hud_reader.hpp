// 共享内存读取端（overlay 侧）。
//
// 与插件的 seqlock 协议配对：
//   写者 publish() 前后各递增一次 seq。奇数 = 正在写。
//   读者读前读后各取一次 seq，两次相同且为偶数 → 快照一致。
//
// 关于形式正确性：seqlock 在 C++ 内存模型下严格来说是数据竞争
// （payload 的非原子读写会与写者重叠），标准没有为它定义行为。
// 实践上它在 ARM64 + acquire/release 屏障下可靠，Linux 内核用了几十年。
// 我们的保障来自两点：
//   1. 只有 seq 前后一致的快照才被采纳，撕裂数据会被丢弃重读；
//   2. tests/test_seqlock.cpp 用真线程对跑，按写者不变量逐条校验，
//      850 万份快照零撕裂（ASan 与 TSan 各跑一轮均无报告）。
// 这是经验证据，不是形式证明——但对一个血条 HUD 足够了。
//
// 不依赖任何 Switch 头文件，可在宿主机上跑并发测试。

#ifndef SS_HUD_READER_HPP
#define SS_HUD_READER_HPP

#include <stdint.h>
#include <string.h>

#include "../../shared/hud_protocol.h"

namespace sshud {

/// 撕裂重试上限。写者只在受击时更新，撞上的概率极低，
/// 但要有上限，否则写者卡死会把 overlay 一起拖住。
constexpr int SNAPSHOT_MAX_RETRIES = 8;

enum class ReadStatus {
  Ok,            // 快照有效
  NotAttached,   // 还没映射共享内存
  BadMagic,      // 插件没起来，或内存不是我们的
  VersionMismatch,  // 插件与 overlay 版本不一致，需要一起更新
  Torn,          // 重试用尽仍未读到一致快照
};

class HudReader {
 public:
  void attach(const SsHudBlock* block) { block_ = block; }
  bool isAttached() const { return block_ != nullptr; }

  /// 读取一份一致快照。失败时 out 内容未定义，调用方必须看返回值。
  ReadStatus snapshot(SsHudBlock* out) const {
    if (!block_ || !out) return ReadStatus::NotAttached;

    for (int attempt = 0; attempt < SNAPSHOT_MAX_RETRIES; ++attempt) {
      const uint64_t s1 = __atomic_load_n(&block_->seq, __ATOMIC_ACQUIRE);
      if (s1 & 1u) continue;  // 写入中

      __atomic_thread_fence(__ATOMIC_ACQUIRE);
      memcpy(out, block_, sizeof(SsHudBlock));
      __atomic_thread_fence(__ATOMIC_ACQUIRE);

      const uint64_t s2 = __atomic_load_n(&block_->seq, __ATOMIC_ACQUIRE);
      if (s1 != s2) continue;  // 读期间被改写，重来

      if (out->magic != SSHUD_MAGIC) return ReadStatus::BadMagic;
      if (out->version != SSHUD_VERSION) return ReadStatus::VersionMismatch;

      // 防御：entityCount 来自另一个进程，不能信任
      if (out->entityCount > SSHUD_MAX_ENTITIES) out->entityCount = SSHUD_MAX_ENTITIES;
      return ReadStatus::Ok;
    }
    return ReadStatus::Torn;
  }

  /// 人类可读的状态说明，直接画在 overlay 上做诊断。
  static const char* statusText(ReadStatus st) {
    switch (st) {
      case ReadStatus::Ok:              return "OK";
      case ReadStatus::NotAttached:     return "未连接共享内存";
      case ReadStatus::BadMagic:        return "插件未加载";
      case ReadStatus::VersionMismatch: return "插件与 overlay 版本不符";
      case ReadStatus::Torn:            return "读取冲突";
    }
    return "未知";
  }

  /// 把 hookStatus 位图翻成诊断文本，告诉用户卡在哪一步。
  static const char* hookDiagnosis(uint32_t hookStatus) {
    if (!(hookStatus & SSHUD_HOOK_INIT_OK))    return "插件未初始化";
    if (!(hookStatus & SSHUD_HOOK_BUILDID_OK)) return "游戏版本与 offsets 不符";
    if (!(hookStatus & SSHUD_HOOK_TAKEDAMAGE)) return "TakeDamage hook 未安装";
    return nullptr;  // 一切正常
  }

 private:
  const SsHudBlock* block_ = nullptr;
};

/// 归一化坐标 [0,1] → libtesla 的 720p 屏幕空间。
/// 研究结论：libtesla 的 framebuffer 像素与 1280x720 游戏屏幕空间 1:1 对应。
constexpr int TESLA_SCREEN_W = 1280;
constexpr int TESLA_SCREEN_H = 720;

inline int normToScreenX(float nx) {
  if (nx < 0.0f) nx = 0.0f;
  if (nx > 1.0f) nx = 1.0f;
  return (int)(nx * (float)TESLA_SCREEN_W);
}

inline int normToScreenY(float ny) {
  if (ny < 0.0f) ny = 0.0f;
  if (ny > 1.0f) ny = 1.0f;
  return (int)(ny * (float)TESLA_SCREEN_H);
}

/// 血条填充比例 [0,1]。maxHp 非法时返回 0，避免除零和越界绘制。
inline float hpFraction(const SsHudEntity& e) {
  if (e.maxHp <= 0) return 0.0f;
  int32_t hp = e.hp;
  if (hp < 0) hp = 0;
  if (hp > e.maxHp) hp = e.maxHp;
  return (float)hp / (float)e.maxHp;
}

/// 受击后经过的秒数，用于淡出。
inline double secondsSinceHit(const SsHudBlock& b, const SsHudEntity& e) {
  if (!b.tickFreq || b.frameTick < e.lastHitTick) return 0.0;
  return (double)(b.frameTick - e.lastHitTick) / (double)b.tickFreq;
}

}  // namespace sshud

#endif  // SS_HUD_READER_HPP
