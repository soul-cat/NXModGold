// 4 号位（BOSS 挑战用的 100% 存档）永不写盘：BOSS 挑战会在内存里改 defeated* 等标记，存进去就把存档弄乱了。
//
// 拦最底层的两个落盘出口（反汇编核实调用关系），按文件路径过滤，只拦 4 号位的文件（isBossSlotPath）：
//   SwitchSaveHandler.WriteFile(path, bytes, callback)   存档 user{N}.dat（WriteSaveSlot）、内部 WriteAllBytes、shared.dat
//   SwitchFileSystemHelper.WriteAllBytes(path, bytes)     恢复点 Restore_Points{N}/、版本备份 user{N}_<版本>.dat
// 第一版拦的是 WriteSaveSlot / WriteSaveRestorePoint，漏了读到旧版本存档时的升级写入和版本备份，所以下移到这里。
// 拦下后回调 callback(true)（没有回调的是同步写，直接返回），游戏照常继续。其他存档位原样放行。

#pragma once

#include "boss_rush_calls.hpp"
#include "boss_rush_model.hpp"

namespace sshud::bossrush {

constexpr int LOGGED_WRITE_LIMIT = 20;

/// 路径转 ASCII；每次写盘记一行日志（写盘不频繁，前 LOGGED_WRITE_LIMIT 次）
inline bool shouldSkipWrite(ui::ManagedString path) {
  static int s_logged = 0;
  char ascii[PATH_CAPACITY];
  options::asciiOf(path, ascii, sizeof(ascii));
  const bool isSkipped = isBossSlotPath(ascii);
  if (++s_logged <= LOGGED_WRITE_LIMIT) Logging.Log("[sshud] save write %s -> %s", ascii, isSkipped ? "skipped" : "written");
  return isSkipped;
}

}  // namespace sshud::bossrush

// IL2CPP 签名：void (SwitchSaveHandler* self, string path, byte[] bytes, Action<bool> callback, const MethodInfo*)
HOOK_DEFINE_TRAMPOLINE(SaveHandlerWriteFileHook) {
  static void Callback(void* self, void* path, void* bytes, void* callback, const void* method) {
    if (!sshud::bossrush::shouldSkipWrite(path)) return Orig(self, path, bytes, callback, method);
    if (callback != nullptr) sshud::bossrush::call<void>(ACTION_BOOL_INVOKE, callback, true);
  }
};

// IL2CPP 签名：static void (string path, byte[] bytes, const MethodInfo*)
HOOK_DEFINE_TRAMPOLINE(FileSystemWriteAllBytesHook) {
  static void Callback(void* path, void* bytes, const void* method) {
    if (!sshud::bossrush::shouldSkipWrite(path)) Orig(path, bytes, method);
  }
};
