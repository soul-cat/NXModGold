// BOSS 挑战（4 号位专用）的运行时：每帧读手柄 → 菜单 → 执行命令；传送后到场再做修正。
//
// 流程：按住 R3 按 ZR 打开菜单（锁住角色操作）→ 选 BOSS → 改标记（boss_rush_table.hpp）→ 切到 BOSS 场景的门
// → 到场约 1 秒后激活表里写的物体（有的 BOSS 要手动叫出来）。4 号位不存档（boss_rush_save_guard.hpp）。
// 关菜单后等 A / B 松开才还操作，免得确认键被游戏当成跳跃 / 冲刺。

#pragma once

#include "boss_rush_calls.hpp"
#include "boss_rush_model.hpp"
#include "boss_rush_save_guard.hpp"
#include "boss_rush_texts.hpp"
#include "toggles.hpp"

namespace sshud::bossrush {

constexpr float FIXUP_DELAY_SECONDS = 1.0f;    // 到场后等场景里的物体初始化完
constexpr float FIXUP_TIMEOUT_SECONDS = 30.0f;  // 这么久还没到场就放弃（传送被游戏拒绝等）

struct PendingFixup {
  const BossEntry* boss = nullptr;
  float readyAt = 0.0f;  // 0 = 还没到场
  float deadline = 0.0f;
};

inline BossMenu g_menu;
inline PendingFixup g_fixup;
inline bool g_isWaitingRelease = false;

inline void startTravel(const BossEntry& boss, float now) {
  applyFlags(boss);
  if (!travelTo(boss.scene, boss.gate)) {
    Logging.Log("[sshud] boss rush: travel to %s refused (scene transition in progress)", boss.scene);
    return;
  }
  if (boss.activatePath != nullptr) g_fixup = {&boss, 0.0f, now + FIXUP_TIMEOUT_SECONDS};
}

inline void runCommand(const MenuResult& result, float now) {
  if (result.command == Command::Opened) {
    ensureTexts();
    setInputAccepted(false);
    Logging.Log("[sshud] boss rush: menu opened");
    return;
  }
  g_isWaitingRelease = true;
  if (result.command == Command::Rest && !travelTo(REST_SCENE, REST_GATE)) {
    Logging.Log("[sshud] boss rush: return to rest refused");
  }
  if (result.command == Command::Refill) refillHero();
  if (result.command == Command::Travel && result.bossIndex >= 0) startTravel(BOSSES[result.bossIndex], now);
}

/// 到场（场景名对上且不在切场景）后等 FIXUP_DELAY_SECONDS 再激活
inline void updateFixup(float now) {
  if (g_fixup.boss == nullptr) return;
  if (now > g_fixup.deadline) {
    Logging.Log("[sshud] boss rush: gave up waiting for %s", g_fixup.boss->scene);
    g_fixup = PendingFixup{};
    return;
  }
  if (!isInScene(g_fixup.boss->scene)) return;
  if (g_fixup.readyAt == 0.0f) g_fixup.readyAt = now + FIXUP_DELAY_SECONDS;
  if (now < g_fixup.readyAt) return;
  const bool isDone = activatePath(g_fixup.boss->activatePath);
  Logging.Log("[sshud] boss rush: activate %s -> %s", g_fixup.boss->activatePath, isDone ? "ok" : "not found");
  g_fixup = PendingFixup{};
}

inline void releaseInputWhenIdle(uint64_t buttons) {
  if (!g_isWaitingRelease || (buttons & (BUTTON_A | BUTTON_B)) != 0) return;
  g_isWaitingRelease = false;
  setInputAccepted(true);
}

/// 每帧调用（CameraController.LateUpdate）；返回菜单是否开着（开着时快捷面板不处理输入）
inline bool update(float now) {
  const uint64_t buttons = toggles::currentButtons();
  const bool isRelevant = g_menu.isOpen() || (buttons & BUTTON_STICK_R) != 0;  // 平时不做托管调用
  const MenuResult result = g_menu.update(buttons, isRelevant && canUseNow());
  if (result.command != Command::None) runCommand(result, now);
  releaseInputWhenIdle(buttons);
  updateFixup(now);
  return g_menu.isOpen();
}

inline void installHooks() {
  SaveHandlerWriteFileHook::InstallAtOffset(SSH_WRITEFILE);
  FileSystemWriteAllBytesHook::InstallAtOffset(SFSH_WRITEALLBYTES);
  Logging.Log("[sshud] boss rush hooks installed: SwitchSaveHandler.WriteFile, SwitchFileSystemHelper.WriteAllBytes (slot %d never saves)",
              BOSS_SLOT);
}

}  // namespace sshud::bossrush
