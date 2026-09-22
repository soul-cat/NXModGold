// 插件（游戏进程内）与 Tesla overlay 之间的共享内存协议。
// 两侧必须使用同一份头文件；改动此文件后两侧都要重新编译。
//
// 并发模型：单写者（插件）+ 多读者（overlay），使用 seqlock。
// 写者在写入前后各自递增 seq；读者看到奇数或前后 seq 不一致就重读。
// 不使用互斥锁——游戏主线程绝不能因为 overlay 而阻塞。

#ifndef SS_HUD_PROTOCOL_H
#define SS_HUD_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SSHUD_MAGIC         0x44485353u  // 'SSHD' (小端)
#define SSHUD_VERSION       1u

// 同屏最多追踪的敌人数量。超出的按"最近受击"淘汰。
//
// 为什么是 8 而不是更多：SaltyNX 的 SharedMemory 一共只有 0x1000 字节，
// 且由所有插件共用（NX-FPS 占 174 字节）。我们按需申请一段，
// 8 个槽位 = 48 + 8*48 = 432 字节，约占总量 10%，是个讲道理的数字。
// 何况屏幕固定血条列超过 8 条本来也没法看。
#define SSHUD_MAX_ENTITIES  8

// SaltyNX SharedMemory 的总大小。我们只申请其中一小段，
// 偏移由 SaltySD_CheckIfSharedMemoryAvailable() 返回。
#define SSHUD_SHMEM_TOTAL   0x1000

// SsHudEntity.flags 位定义
#define SSHUD_FLAG_ALIVE      (1u << 0)  // HealthManager 仍然有效
#define SSHUD_FLAG_BOSS       (1u << 1)  // maxHp 超过 BOSS 阈值
#define SSHUD_FLAG_POS_VALID  (1u << 2)  // screenX/screenY 可用
#define SSHUD_FLAG_INVINCIBLE (1u << 3)  // 当前处于无敌帧

/// 单个被追踪敌人的状态快照
typedef struct {
  uint64_t objectId;      // IL2CPP 对象指针，用作身份标识（不解引用，仅比对）
  int32_t  hp;            // 当前血量
  int32_t  maxHp;         // 最大血量（首次观测到的 hp 或 HealthManager 字段）
  int32_t  lastDamage;    // 最近一次受到的伤害
  uint32_t flags;         // SSHUD_FLAG_*
  float    screenX;       // 归一化屏幕坐标 [0,1]，左->右
  float    screenY;       // 归一化屏幕坐标 [0,1]，上->下
  uint64_t lastHitTick;   // 最近受击时的 CNTVCT_EL0 值
  uint32_t nameHash;      // GameObject 名字的 FNV-1a 32 位哈希，供 overlay 查表
  uint32_t reserved;
} SsHudEntity;

/// 共享内存主块。
///
/// 布局约定（与 NX-FPS 一致）：magic 必须是本结构的第一个 4 字节字段，
/// overlay 侧以 4 字节步长扫描整个 0x1000 区域来定位我们的块。
typedef struct {
  uint32_t    magic;          // SSHUD_MAGIC，overlay 用它确认插件已就绪
  uint32_t    version;        // SSHUD_VERSION，不匹配时 overlay 拒绝读取
  volatile uint64_t seq;      // seqlock 序号：偶数=稳定，奇数=写入中
  uint64_t    tickFreq;       // CNTFRQ_EL0，overlay 用它把 tick 换算成秒
  uint64_t    frameTick;      // 最近一次更新时的 CNTVCT_EL0
  uint64_t    buildIdLow;     // 游戏 Build ID 前 8 字节，overlay 可据此提示版本不符
  uint32_t    entityCount;    // entities 中有效条目数
  uint32_t    hookStatus;     // SSHUD_HOOK_* 位图，用于在 overlay 上显示诊断
  SsHudEntity entities[SSHUD_MAX_ENTITIES];
} SsHudBlock;

// hookStatus 位定义 —— 让 overlay 能直观告诉用户哪一步没成功
#define SSHUD_HOOK_INIT_OK      (1u << 0)  // 插件已加载
#define SSHUD_HOOK_BUILDID_OK   (1u << 1)  // Build ID 与 offsets.h 匹配
#define SSHUD_HOOK_TAKEDAMAGE   (1u << 2)  // TakeDamage hook 已安装
#define SSHUD_HOOK_CAMERA       (1u << 3)  // 相机参数可读，坐标换算可用

/// 本块实际占用的字节数，申请共享内存时传给
/// SaltySD_CheckIfSharedMemoryAvailable()。必须 4 字节对齐（该 API 的要求）。
#define SSHUD_BLOCK_SIZE    ((int32_t)sizeof(SsHudBlock))

/// FNV-1a 32 位哈希 —— 插件和 overlay 必须用同一个实现
static inline uint32_t sshudHashName(const char* s) {
  uint32_t h = 2166136261u;
  if (!s) return 0;
  while (*s) {
    h ^= (uint8_t)(*s++);
    h *= 16777619u;
  }
  return h;
}

#ifdef __cplusplus
}
#endif

#endif  // SS_HUD_PROTOCOL_H
