// SD 卡日志：SD:/config/sshud/log.txt（Ryujinx 里是 datas/portable/sdcard/config/sshud/log.txt）。
//
// exl_main 时游戏的 fs 还没准备好，所以先把日志缓冲在内存里；游戏跑起来后
// （首个 CameraController.LateUpdate）调 sdlog::start() 挂 SD、建新文件、把缓冲写进去。
// 之后日志先攒进同一块缓冲，每帧 sdlog::flushIfDue() 至多每 2 秒（或缓冲满时）整批
// 开 → 写（flush）→ 关一次：逐行写 SD 每行要几毫秒会卡主线程；文件不常驻打开，游戏运行中也能远程读取。
// 代价：崩溃前最后 ≤2 秒的日志可能丢失。
// 挂载需要 npdm 的 fs 权限（tools/40-patch-npdm.py 已放开）。任何一步失败只关掉本日志，不影响游戏。

#pragma once

#include <lib/log/ilogger.hpp>

namespace sshud {

struct SdLogger : public exl::log::ILogger {
  virtual void LogRaw(std::string_view text) final;
};

namespace sdlog {

void start();
/// 每帧调用：距上次定时写出满 2 秒且缓冲非空时整批写出
void flushIfDue(float now);

}  // namespace sdlog

}  // namespace sshud
