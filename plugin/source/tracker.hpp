// 敌人状态追踪器。
//
// 设计约束：
//   - 运行在游戏主线程的 hook 里，必须无分配、无锁、常数时间。
//   - 槽位数固定（SSHUD_MAX_ENTITIES），满了就淘汰最久未受击的。
//   - 只有 publish() 碰共享内存，用 seqlock 保证 overlay 读到的是一致快照。
//
// 本文件不依赖任何 Switch 头文件，可在宿主机上跑单元测试。

#ifndef SS_TRACKER_HPP
#define SS_TRACKER_HPP

#include <stdint.h>
#include <string.h>

#include "../../shared/hud_protocol.h"

namespace sshud {

/// 敌人受击后血条保留多久（秒）。超时的条目不再发布。
constexpr double ENTITY_TTL_SECONDS = 4.0;

/// maxHp 超过此值视为 Boss，overlay 会用独立样式在顶部渲染。
/// 取值参考 PC 端 ShowDamage HealthBar 的 105 阈值。
constexpr int32_t BOSS_HP_THRESHOLD = 105;

class EntityTracker {
 public:
  void init(SsHudBlock* block, uint64_t tickFreq, uint64_t buildIdLow) {
    block_ = block;
    memset(slots_, 0, sizeof(slots_));
    slotCount_ = 0;
    if (!block_) return;
    memset(block_, 0, sizeof(*block_));
    block_->magic = SSHUD_MAGIC;
    block_->version = SSHUD_VERSION;
    block_->tickFreq = tickFreq;
    block_->buildIdLow = buildIdLow;
    block_->seq = 0;
    tickFreq_ = tickFreq ? tickFreq : 19200000;  // Switch CNTFRQ 兜底值
  }

  void setHookStatus(uint32_t bits) {
    if (block_) block_->hookStatus |= bits;
  }

  /// 敌人受击时调用。objectId 用 IL2CPP 对象指针，只作身份比对，绝不解引用。
  void onDamage(uint64_t objectId, int32_t hp, int32_t maxHp,
                int32_t damage, uint32_t nameHash, uint64_t nowTick) {
    if (objectId == 0) return;

    Slot* s = findSlot(objectId);
    if (!s) s = claimSlot(nowTick);
    if (!s) return;

    if (s->e.objectId != objectId) {
      // 新条目：初始化 maxHp。有些敌人拿不到 maxHP 字段，
      // 此时用首次观测到的 hp + 本次伤害作为上限估计。
      memset(&s->e, 0, sizeof(s->e));
      s->e.objectId = objectId;
      s->e.maxHp = maxHp > 0 ? maxHp : (hp + damage);
    }
    if (maxHp > 0) s->e.maxHp = maxHp;
    if (hp > s->e.maxHp) s->e.maxHp = hp;  // 防御：maxHp 读错时至少不会越界

    s->e.hp = hp;
    s->e.lastDamage = damage;
    s->e.nameHash = nameHash;
    s->e.lastHitTick = nowTick;
    s->e.flags = SSHUD_FLAG_ALIVE;
    if (s->e.maxHp >= BOSS_HP_THRESHOLD) s->e.flags |= SSHUD_FLAG_BOSS;
    if (hp <= 0) s->e.flags &= ~SSHUD_FLAG_ALIVE;
    s->used = true;
  }

  /// 敌人死亡时调用（hook Die 或观测到 hp<=0）。
  void onDeath(uint64_t objectId) {
    Slot* s = findSlot(objectId);
    if (s) s->e.flags &= ~SSHUD_FLAG_ALIVE;
  }

  /// 把当前快照写进共享内存。由 hook 末尾或定时器调用。
  void publish(uint64_t nowTick) {
    if (!block_) return;

    const uint64_t ttl = (uint64_t)(ENTITY_TTL_SECONDS * (double)tickFreq_);

    // seqlock 写入：奇数表示写入中
    __atomic_store_n(&block_->seq, block_->seq + 1, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);

    uint32_t n = 0;
    for (uint32_t i = 0; i < SSHUD_MAX_ENTITIES; ++i) {
      Slot& s = slots_[i];
      if (!s.used) continue;
      if (nowTick > s.e.lastHitTick && (nowTick - s.e.lastHitTick) > ttl) {
        s.used = false;
        continue;
      }
      block_->entities[n++] = s.e;
    }
    block_->entityCount = n;
    block_->frameTick = nowTick;

    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    __atomic_store_n(&block_->seq, block_->seq + 1, __ATOMIC_RELEASE);
  }

  // --- 测试用访问器 ---
  uint32_t liveSlotCount() const {
    uint32_t n = 0;
    for (uint32_t i = 0; i < SSHUD_MAX_ENTITIES; ++i)
      if (slots_[i].used) ++n;
    return n;
  }

 private:
  struct Slot {
    SsHudEntity e;
    bool used;
  };

  Slot* findSlot(uint64_t objectId) {
    for (uint32_t i = 0; i < SSHUD_MAX_ENTITIES; ++i)
      if (slots_[i].used && slots_[i].e.objectId == objectId) return &slots_[i];
    return nullptr;
  }

  /// 找空槽；没有空槽就淘汰最久未受击的那个。
  Slot* claimSlot(uint64_t nowTick) {
    (void)nowTick;
    Slot* oldest = nullptr;
    for (uint32_t i = 0; i < SSHUD_MAX_ENTITIES; ++i) {
      if (!slots_[i].used) return &slots_[i];
      if (!oldest || slots_[i].e.lastHitTick < oldest->e.lastHitTick)
        oldest = &slots_[i];
    }
    return oldest;
  }

  SsHudBlock* block_ = nullptr;
  uint64_t tickFreq_ = 19200000;
  Slot slots_[SSHUD_MAX_ENTITIES] = {};
  uint32_t slotCount_ = 0;
};

}  // namespace sshud

#endif  // SS_TRACKER_HPP
