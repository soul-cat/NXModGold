// 4 号位（BOSS 挑战用的 100% 存档）永不写盘：BOSS 挑战会在内存里改 defeated* 等标记，存进去就把存档弄乱了。
//
// 拦最底层的两个写入口，存档 / 自动存档 / 恢复点 / 备份都经过它们：
//   SwitchPlatform.WriteSaveSlot(slotIndex, bytes, callback)            文件名 user{slotIndex}.dat（反汇编核实）
//   SwitchSaveRestoreHandler.WriteSaveRestorePoint(slot, …, callback)   Restore_Points{slot}/
// 只拦 slot == 4，其他存档位原样放行。拦下后直接回调 callback(true)，游戏照常继续（与 PC 版 BOSS mod 拦存档的做法一致）。
// 想恢复 4 号位的原样：回标题重新读档即可（盘上的文件从没被改过）。

#pragma once

#include "boss_rush_calls.hpp"

namespace sshud::bossrush {

constexpr int LOGGED_SKIP_LIMIT = 5;

/// 跳过一次写入并告诉游戏写成功了
inline void skipSaveWrite(const char* kind, int32_t slot, void* callback) {
  static int s_skipped = 0;
  if (++s_skipped <= LOGGED_SKIP_LIMIT) Logging.Log("[sshud] boss rush: skipped %s write for slot %d (#%d)", kind, slot, s_skipped);
  if (callback != nullptr) call<void>(ACTION_BOOL_INVOKE, callback, true);
}

}  // namespace sshud::bossrush

// IL2CPP 签名：void (SwitchPlatform* self, int32 slotIndex, byte[] bytes, Action<bool> callback, const MethodInfo*)
HOOK_DEFINE_TRAMPOLINE(WriteSaveSlotHook) {
  static void Callback(void* self, int32_t slotIndex, void* bytes, void* callback, const void* method) {
    if (slotIndex != sshud::bossrush::BOSS_SLOT) return Orig(self, slotIndex, bytes, callback, method);
    sshud::bossrush::skipSaveWrite("save", slotIndex, callback);
  }
};

// IL2CPP 签名：void (SwitchSaveRestoreHandler* self, int32 slot, string identifier, bool noDelete, byte[] bytes,
//                    Action<bool> callback, const MethodInfo*)
HOOK_DEFINE_TRAMPOLINE(WriteRestorePointHook) {
  static void Callback(void* self, int32_t slot, void* identifier, bool noDelete, void* bytes, void* callback,
                       const void* method) {
    if (slot != sshud::bossrush::BOSS_SLOT) return Orig(self, slot, identifier, noDelete, bytes, callback, method);
    sshud::bossrush::skipSaveWrite("restore point", slot, callback);
  }
};
