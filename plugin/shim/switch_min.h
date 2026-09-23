// libnx_min 的精简 switch_min.h —— 只包含本插件实际用到的子集。
// 原版是全量 include，其中 mutex.h/applets/error.h 等与 GCC 16 自带的
// newlib 头冲突（RMutex 字段名变更），逐个绕开不如收窄。
// 需要更多 API 时从 vendor/nx-fps-libnx/include/switch_min.h 按需添加。
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>  // mode_t 等，SaltySD 头的 fopen 声明需要

#include "switch_min/types.h"
#include "switch_min/result.h"

#include "switch_min/arm/tls.h"
#include "switch_min/arm/cache.h"
#include "switch_min/arm/atomics.h"
#include "switch_min/arm/counter.h"

#include "switch_min/kernel/svc.h"
#include "switch_min/kernel/shmem.h"
#include "switch_min/kernel/virtmem.h"
#include "switch_min/kernel/detect.h"
#include "switch_min/kernel/random.h"

#include "switch_min/runtime/env.h"

#ifdef __cplusplus
}
#endif
