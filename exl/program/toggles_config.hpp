// 功能开关的持久化：sd:/config/sshud/config.ini（每行 key=0/1，格式见 toggles_model.hpp）。
// 任何 fs 失败都只记日志，开关保持内存里的值。

#pragma once

#include "toggles_model.hpp"

namespace sshud::toggles {

/// 从 sd:/config/sshud/config.ini 读开关；文件不存在就写一份默认的。SD 挂载之后调用。
void loadSettings(Settings& settings);

/// 把开关写回 sd:/config/sshud/config.ini（先删后建再写，带 flush）
void saveSettings(const Settings& settings);

}  // namespace sshud::toggles
