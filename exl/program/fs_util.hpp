// SD 与游戏存档之间复制文件用的 nn::fs 小工具（save_import / save_export 共用）。
// exlaunch 的 nn/fs 头文件缺的几个函数在这里补声明（游戏 sdk 模块导出）。

#pragma once

#include <lib/nx/result.h>
#include <nn/fs.hpp>

namespace nn::fs {

Result DeleteFile(char const* path);
Result RenameFile(char const* from, char const* to);
Result CommitSaveData(char const* mountName);
Result GetEntryType(DirectoryEntryType* outType, char const* path);

}  // namespace nn::fs

namespace sshud::fsutil {

bool isFile(const char* path);
bool isDirectory(const char* path);

/// 整个文件复制到 to（先删后建，按块读写，带 flush）
Result copyFile(const char* from, const char* to);

}  // namespace sshud::fsutil
