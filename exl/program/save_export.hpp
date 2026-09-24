// 按需把整个游戏存档原样导出到 SD（不依赖 JKSV）：save:/ 下所有文件，含 Restore_Points 子目录。
//
// 触发：SD:/config/sshud/export.request 存在时，游戏启动后首帧（先于 save_import）：
//   1. 导出到 SD:/config/sshud/export/<n>/，n 取第一个还不存在的编号，旧导出永不覆盖；
//   2. 删除触发文件，避免每次启动都导出。
// 只读游戏存档，不改任何东西。每个文件和汇总都写进 SD 日志，便于远程核对大小。

#pragma once

namespace sshud::saveexport {

/// 在 SD 卡挂载之后调用；只执行一次
void runOnce();

}  // namespace sshud::saveexport
