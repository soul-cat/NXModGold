// BOSS 挑战（4 号位专用）的运行时：每帧读手柄 → 菜单 → 执行命令；传送后跟踪到场。
//
// 流程：按住 R3 按 ZR 打开菜单（锁操作 + 无敌）→ 选 BOSS → 改 PlayerData 标记与场景持久标记（boss_rush_table.hpp）
// → 时间流速复位 → 切到 BOSS 场景的门 → 到场后再复位一次时间、约 1 秒后做表里的修正、解除无敌。
// 传送途中每秒记一行（是否在切场景、时间流速、场景名）并立即写盘：之前在 BOSS 战中途切走会卡死，
// 日志能区分「卡在加载」（还在记）和「主线程停了」（断了）。4 号位不存档（boss_rush_save_guard.hpp）。
// 关菜单后等 A / B 松开才还操作，免得确认键被游戏当成跳跃 / 冲刺。

#pragma once

#include "boss_rush_calls.hpp"
#include "boss_rush_model.hpp"
#include "boss_rush_save_guard.hpp"
#include "boss_rush_texts.hpp"
#include "sd_logger.hpp"
#include "toggles.hpp"

#include <cstring>

namespace sshud::bossrush {

constexpr float ARRIVAL_SETTLE_SECONDS = 1.0f;  // 到场后等场景里的物体初始化完
constexpr float TRAVEL_TIMEOUT_SECONDS = 30.0f;  // 这么久还没到场就放弃跟踪
constexpr float TRAVEL_LOG_INTERVAL = 1.0f;

struct PendingTravel {
  const char* scene = nullptr;       // nullptr = 没有进行中的传送
  const BossEntry* boss = nullptr;   // 回休息点时为 nullptr
  float arrivedAt = 0.0f;            // 0 = 还没到场
  float deadline = 0.0f;
  float nextLogAt = 0.0f;
};

inline BossMenu g_menu;
inline PendingTravel g_travel;
inline bool g_isWaitingRelease = false;

/// 结束一次传送跟踪（到场或超时）：解除无敌
inline void finishTravel(const char* outcome) {
  Logging.Log("[sshud] boss rush: travel to %s %s", g_travel.scene, outcome);
  setInvincible(false);
  g_travel = PendingTravel{};
  sdlog::flushNow();
}

inline void startTravel(const char* scene, const char* gate, const BossEntry* boss, float now) {
  if (boss != nullptr) {
    applyFlags(*boss);
    applySceneBools(*boss);
  }
  resetTime();
  if (!travelTo(scene, gate)) {
    Logging.Log("[sshud] boss rush: travel to %s refused (scene transition in progress)", scene);
    setInvincible(false);
    return;
  }
  g_travel = {scene, boss, 0.0f, now + TRAVEL_TIMEOUT_SECONDS, now};
  sdlog::flushNow();
}

inline void logTravelProgress(float now) {
  if (now < g_travel.nextLogAt) return;
  g_travel.nextLogAt = now + TRAVEL_LOG_INTERVAL;
  void* gm = gameManager();
  char current[options::NAME_CAPACITY];
  Logging.Log("[sshud] boss rush: waiting for %s: inTransition=%d timeScale=%.2f scene=%s", g_travel.scene,
              gm != nullptr && call<bool>(GAMEMGR_GETISINSCENETRANSITION, gm) ? 1 : 0, call<float>(TIMEMANAGER_GETTIMESCALE),
              gm != nullptr ? options::asciiOf(ui::readField<ui::ManagedString>(gm, GAMEMGR_SCENENAME), current, sizeof(current)) : "?");
  sdlog::flushNow();
}

/// 到场（场景名对上且不在切场景）：复位时间，等 ARRIVAL_SETTLE_SECONDS 后做修正并结束跟踪
inline void updateTravel(float now) {
  if (g_travel.scene == nullptr) return;
  if (now > g_travel.deadline) return finishTravel("timed out");
  if (g_travel.arrivedAt == 0.0f) {
    logTravelProgress(now);
    if (!isInScene(g_travel.scene)) return;
    g_travel.arrivedAt = now;
    resetTime();
  }
  if (now < g_travel.arrivedAt + ARRIVAL_SETTLE_SECONDS) return;
  const char* path = g_travel.boss != nullptr ? g_travel.boss->activatePath : nullptr;
  if (path != nullptr) Logging.Log("[sshud] boss rush: activate %s -> %s", path, activatePath(path) ? "ok" : "not found");
  finishTravel("arrived");
}

inline void runCommand(const MenuResult& result, float now) {
  if (result.command == Command::Opened) {
    ensureTexts();
    setInputAccepted(false);
    setInvincible(true);
    Logging.Log("[sshud] boss rush: menu opened");
    return;
  }
  g_isWaitingRelease = true;
  if (result.command == Command::Rest) return startTravel(REST_SCENE, REST_GATE, nullptr, now);
  if (result.command == Command::Travel && result.bossIndex >= 0) {
    const BossEntry& boss = BOSSES[result.bossIndex];
    return startTravel(boss.scene, boss.gate, &boss, now);
  }
  if (result.command == Command::Refill) refillHero();
  setInvincible(false);  // 关菜单 / 回满：不传送就立刻解除无敌
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
  updateTravel(now);
  return g_menu.isOpen();
}

constexpr uintptr_t PERSISTENT_DATA_ID = 0x10;          // PersistentItemData<bool>.ID
constexpr uintptr_t PERSISTENT_DATA_SCENE = 0x18;       // .SceneName
constexpr uintptr_t PERSISTENT_DATA_VALUE = 0x21;       // .Value

/// 传送途中游戏要把 (scene, id) 写回场景数据时，如果是这个 BOSS 要重置的标记，改成表里的值。
/// 从 BOSS 场景直接重进同一场景时，离开旧场景那一刻游戏会把「战斗已打完」写回去，盖掉进场前的重置。
inline void overridePersistentWrite(void* data) {
  const BossEntry* boss = g_travel.boss;
  if (boss == nullptr || boss->sceneBoolCount == 0 || data == nullptr) return;
  char scene[options::NAME_CAPACITY];
  char id[options::NAME_CAPACITY];
  if (std::strcmp(options::asciiOf(ui::readField<ui::ManagedString>(data, PERSISTENT_DATA_SCENE), scene, sizeof(scene)),
                  boss->scene) != 0) {
    return;
  }
  options::asciiOf(ui::readField<ui::ManagedString>(data, PERSISTENT_DATA_ID), id, sizeof(id));
  for (size_t i = 0; i < boss->sceneBoolCount; ++i) {
    if (std::strcmp(id, boss->sceneBools[i].id) != 0) continue;
    ui::writeField<bool>(data, PERSISTENT_DATA_VALUE, boss->sceneBools[i].value);
    Logging.Log("[sshud] boss rush: kept scene bool %s/%s=%d while leaving", scene, id, boss->sceneBools[i].value ? 1 : 0);
  }
}

}  // namespace sshud::bossrush

// IL2CPP 签名：void (PersistentItemDataCollection<bool, …>* self, PersistentItemData<bool>* data, const MethodInfo*)
HOOK_DEFINE_TRAMPOLINE(PersistentBoolSetValueHook) {
  static void Callback(void* self, void* data, const void* method) {
    sshud::bossrush::overridePersistentWrite(data);
    Orig(self, data, method);
  }
};

namespace sshud::bossrush {

inline void installHooks() {
  PersistentBoolSetValueHook::InstallAtOffset(PDC_BOOL_SETVALUE);
  SaveHandlerWriteFileHook::InstallAtOffset(SSH_WRITEFILE);
  FileSystemWriteAllBytesHook::InstallAtOffset(SFSH_WRITEALLBYTES);
  Logging.Log("[sshud] boss rush hooks installed: PersistentItemDataCollection<bool>.SetValue, SwitchSaveHandler.WriteFile, "
              "SwitchFileSystemHelper.WriteAllBytes (slot %d never saves)",
              BOSS_SLOT);
}

}  // namespace sshud::bossrush
