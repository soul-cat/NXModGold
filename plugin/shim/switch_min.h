// 兼容 shim：SaltyNX 的头文件引用老版 libnx 的 <switch_min.h>，
// 现代 libnx 已将其并入 <switch.h>（所需的 u64/Result/Handle 均在其中）。
#ifndef SWITCH_MIN_SHIM_H
#define SWITCH_MIN_SHIM_H
#include <switch.h>
#endif
