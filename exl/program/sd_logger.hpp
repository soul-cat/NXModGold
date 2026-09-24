// SD 卡日志：SD:/config/sshud/log.txt（Ryujinx 里是 datas/portable/sdcard/config/sshud/log.txt）。
//
// exl_main 时游戏的 fs 还没准备好，所以先把日志缓冲在内存里；游戏跑起来后
// （首个 CameraController.LateUpdate）调 sdlog::start() 挂 SD、建新文件、把缓冲写进去，
// 之后每行都是 开 → 写（flush）→ 关：实机崩溃前的最后几行也能留下，游戏运行中也能远程读取。
// 挂载需要 npdm 的 fs 权限（tools/40-patch-npdm.py 已放开）。任何一步失败只关掉本日志，不影响游戏。

#pragma once

#include <lib/log/ilogger.hpp>

namespace sshud {

struct SdLogger : public exl::log::ILogger {
  virtual void LogRaw(std::string_view text) final;
};

namespace sdlog {

void start();

}  // namespace sdlog

}  // namespace sshud
