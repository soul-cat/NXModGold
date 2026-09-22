// overlay 侧的 SaltyNX 共享内存客户端。
//
// 调用序列照抄 Status-Monitor-Overlay/include/SaltyNX.h 的验证过的做法：
//
//   1. 先 ping "InjectServ" 端口确认 SaltyNX 活着且没卡住
//   2. 连 "SaltySD" 端口
//   3. 取共享内存句柄（cmd 7）
//   4. **先** SaltySD_Term() 关会话，句柄在会话关闭后依然有效
//   5. shmemLoadRemote + shmemMap
//   6. 以 4 字节步长扫描整个 0x1000 区域找我们的 magic
//
// 第 4 步的顺序不是笔误 —— 参考实现就是先 Term 再 map。

#ifndef SS_SALTYNX_CLIENT_HPP
#define SS_SALTYNX_CLIENT_HPP

#include <stdint.h>

#include "../../shared/hud_protocol.h"

namespace sshud {

/// 共享内存里以 4 字节步长找 magic。
///
/// SaltyNX 的分配是 4 字节对齐的，所以步长必须是 4；saltysd_proc 内部
/// 的扫描器也是同样约定，两端一致。
///
/// 这个函数不依赖任何 Switch 头文件，可在宿主机上测试。
/// 返回 magic 所在的字节偏移，找不到返回 -1。
inline int32_t findBlockOffset(const uint8_t* shmemBase, int32_t shmemSize,
                               uint32_t magic) {
  if (shmemBase == nullptr) return -1;
  // 整个块必须装得下，否则找到 magic 也没用
  const int32_t limit = shmemSize - SSHUD_BLOCK_SIZE;
  for (int32_t offset = 0; offset <= limit; offset += 4) {
    uint32_t value = 0;
    // 逐字节取，避免对未对齐地址做 32 位读
    for (int b = 0; b < 4; ++b) {
      value |= (uint32_t)shmemBase[offset + b] << (8 * b);
    }
    if (value == magic) return offset;
  }
  return -1;
}

}  // namespace sshud

#endif  // SS_SALTYNX_CLIENT_HPP
