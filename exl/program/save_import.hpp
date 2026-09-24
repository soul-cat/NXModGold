// 从 SD 卡导入存档到游戏存档位（真机测试用：JKSV 不可用时，全程无线放测试存档）。
//
// 把 userN.dat（N = 2..4）放到 SD:/config/sshud/import/，游戏启动后首帧：
//   1. 原有的 save:/userN.dat 先备份到 SD:/config/sshud/import/backup-userN.dat（没有就跳过）；
//   2. 复制新文件进 save:/userN.dat，CommitSaveData("save")；
//   3. 导入文件改名为 userN.dat.done，避免下次启动重复导入。
// 1 号存档位永远不动。任何一步失败都只记日志、不再继续该存档位。
// 挂载名 "save"、文件名 user{0}.dat 来自游戏自己的字符串常量（SwitchPlatform）。

#pragma once

namespace sshud::saveimport {

/// 在 SD 卡挂载之后调用；只执行一次
void runOnce();

}  // namespace sshud::saveimport
