// BOSS 挑战用到的游戏调用：切场景、改 PlayerData 标记与场景持久标记、到场修正（激活物体 / 状态机事件 / 跳状态）、
// 锁操作、无敌、时间复位、回满状态。
//
// 切场景照游戏自己的写法（HeroController.HazardRespawn 反汇编）：object_new(SceneLoadInfo) → .ctor →
// 填 SceneName / EntryGateName → GameManager.BeginSceneTransition。其余字段保持构造默认值，由游戏自己淡出淡入。
// 字符串是直接写进托管字段的（没有写屏障），和 SceneLoadInfo 一起钉住，下一次传送时再放开。
// 全部在 Unity 主线程（CameraController.LateUpdate）。

#pragma once

#include "boss_rush_model.hpp"
#include "boss_rush_table.hpp"
#include "fsm_lookup.hpp"
#include "map_calls.hpp"
#include "map_collected.hpp"
#include "options_menu_calls.hpp"

#include <cstring>

namespace sshud::bossrush {

namespace ui = sshud::unity;
using map::call;
using map::managedString;

constexpr int32_t BOSS_SLOT = 4;  // 4 号位 = 100% 存档，只在这里开放 BOSS 挑战
constexpr size_t PATH_CAPACITY = 128;
constexpr size_t TRAVEL_PIN_COUNT = 3;

inline void* gameManager() { return call<void*>(GAMEMGR_GETINSTANCE); }

inline void* heroOf(void* gm) { return gm != nullptr ? call<void*>(GAMEMGR_GETHEROCTRL, gm) : nullptr; }

/// 4 号位、有角色、不在切场景
inline bool canUseNow() {
  void* gm = gameManager();
  if (gm == nullptr || ui::readField<int32_t>(gm, GAMEMGR_PROFILEID) != BOSS_SLOT) return false;
  return ui::isAlive(heroOf(gm)) && !call<bool>(GAMEMGR_GETISINSCENETRANSITION, gm);
}

/// 菜单打开期间不让角色动（InputHandler.Stop/StartAcceptingInput）
inline void setInputAccepted(bool isAccepted) {
  void* hero = heroOf(gameManager());
  void* input = ui::isAlive(hero) ? ui::readField<void*>(hero, HC_INPUTHANDLER) : nullptr;
  if (!ui::isAlive(input)) return;
  call<void>(isAccepted ? IH_STARTACCEPTINGINPUT : IH_STOPACCEPTINGINPUT, input);
}

inline void refillHero() {
  void* hero = heroOf(gameManager());
  if (ui::isAlive(hero)) call<void>(HC_REFILLALL, hero);
  Logging.Log("[sshud] boss rush: refill");
}

inline FlagLedger g_ledger;
inline const void* g_ledgerPlayerData = nullptr;  // 记账时的 PlayerData；变了（回标题重新读档）就清账

inline void setPlayerDataBool(void* playerData, const char* name, bool value) {
  call<void>(PD_SETBOOL, playerData, managedString(name), value);
}

inline const char* sceneOf(const BossEntry& boss, const SceneBool& sceneBool) {
  return sceneBool.scene != nullptr ? sceneBool.scene : boss.scene;
}

/// 场景持久标记的数据对象：TryGetValue 拿到的是集合里那一份（引用类型），改它的值即生效；从没存过返回 null
inline uint8_t* sceneBoolRecord(const char* scene, const char* id) {
  const map::EntryStrings strings{managedString(scene), managedString(id)};
  return const_cast<uint8_t*>(map::findBoolRecord(strings));
}

inline bool isBossSceneBool(const BossEntry& boss, const char* scene, const char* id) {
  for (size_t i = 0; i < boss.sceneBoolCount; ++i) {
    if (std::strcmp(boss.sceneBools[i].id, id) == 0 && std::strcmp(sceneOf(boss, boss.sceneBools[i]), scene) == 0) return true;
  }
  return false;
}

/// 把账上这次用不到的标记还原成存档原值（boss 为 nullptr 时全部还原）
inline void restoreOthers(void* playerData, const BossEntry* boss) {
  for (size_t i = 0; i < g_ledger.count(); ++i) {
    const FlagLedger::Entry& entry = g_ledger.entries()[i];
    if (entry.scene == nullptr) {
      if (boss == nullptr || !isBossFlag(*boss, entry.name)) setPlayerDataBool(playerData, entry.name, entry.original);
      continue;
    }
    if (boss != nullptr && isBossSceneBool(*boss, entry.scene, entry.name)) continue;
    uint8_t* record = sceneBoolRecord(entry.scene, entry.name);
    if (record != nullptr) ui::writeField<bool>(record, map::PERSISTENT_BOOL_VALUE, entry.original);
  }
}

inline void applyBoss(void* playerData, const BossEntry& boss) {
  for (size_t i = 0; i < boss.flagCount; ++i) {
    const BossFlag& flag = boss.flags[i];
    g_ledger.remember(nullptr, flag.name, call<bool>(PD_GETBOOL, playerData, managedString(flag.name)));
    setPlayerDataBool(playerData, flag.name, flag.value);
  }
  for (size_t i = 0; i < boss.sceneBoolCount; ++i) {
    const SceneBool& sceneBool = boss.sceneBools[i];
    uint8_t* record = sceneBoolRecord(sceneOf(boss, sceneBool), sceneBool.id);
    const bool original = record != nullptr && ui::readField<bool>(record, map::PERSISTENT_BOOL_VALUE);
    g_ledger.remember(sceneOf(boss, sceneBool), sceneBool.id, original);
    if (record != nullptr) ui::writeField<bool>(record, map::PERSISTENT_BOOL_VALUE, sceneBool.value);
  }
}

/// 传送前整理世界状态：还原上一个 BOSS 的改动，再应用这一个的（boss 为 nullptr = 回休息点，全部还原）
inline void prepareWorld(const BossEntry* boss) {
  void* playerData = call<void*>(PD_GETINSTANCE);
  if (playerData == nullptr) return;
  if (playerData != g_ledgerPlayerData) {
    g_ledger.clear();
    g_ledgerPlayerData = playerData;
  }
  restoreOthers(playerData, boss);
  if (boss != nullptr) applyBoss(playerData, *boss);
  Logging.Log("[sshud] boss rush: world prepared for %s (%zu flags tracked)", boss != nullptr ? boss->scene : "rest",
              g_ledger.count());
}

/// 时间流速全部复位成 1（TimeManager.Reset，游戏自己的「退出到主菜单」就这么做）：
/// BOSS 战里挨打的定格 / 慢动作若在切场景时没走完，时间可能停在 0
inline void resetTime() { call<void>(TIMEMANAGER_RESET); }

/// 菜单打开和传送途中无敌（菜单打开时角色被锁住，挨打会触发定格）
inline void setInvincible(bool isInvincible) {
  void* playerData = call<void*>(PD_GETINSTANCE);
  if (playerData != nullptr) call<void>(PD_SETBOOL, playerData, managedString("isInvincible"), isInvincible);
}

/// 被拦下的 BOSS 死亡 / 结局流程会先置 disablePause、disableInventory（苍白之母、初罪者），传送时复位
inline void clearCutsceneLocks() {
  void* playerData = call<void*>(PD_GETINSTANCE);
  if (playerData == nullptr) return;
  call<void>(PD_SETBOOL, playerData, managedString("disablePause"), false);
  call<void>(PD_SETBOOL, playerData, managedString("disableInventory"), false);
}

inline const void* sceneLoadInfoClass() {
  auto* slot = reinterpret_cast<const void* const*>(exl::util::modules::GetTargetOffset(SLI_TYPEINFO));
  if (*slot == nullptr) ui::mainFunction<void (*)(const void*, bool)>(IL2CPP_INIT_METADATA)(slot, true);
  return *slot;
}

/// 上一次传送钉住的对象（SceneLoadInfo 与两个字符串）
inline ui::PinHandle g_travelPins[TRAVEL_PIN_COUNT] = {};

inline void pinForTravel(void* info, ui::ManagedString scene, ui::ManagedString gate) {
  for (ui::PinHandle& pin : g_travelPins) ui::unpinObject(pin);
  g_travelPins[0] = ui::pinObject(info);
  g_travelPins[1] = ui::pinObject(scene);
  g_travelPins[2] = ui::pinObject(gate);
}

/// 切到 scene 的 gate 门；游戏正在切场景时返回 false
inline bool travelTo(const char* scene, const char* gate) {
  void* gm = gameManager();
  const void* klass = sceneLoadInfoClass();
  if (gm == nullptr || klass == nullptr || call<bool>(GAMEMGR_GETISINSCENETRANSITION, gm)) return false;
  void* info = ui::mainFunction<void* (*)(const void*)>(IL2CPP_OBJECT_NEW)(klass);
  if (info == nullptr) return false;
  call<void>(SLI_CTOR, info);
  ui::ManagedString sceneName = managedString(scene);
  ui::ManagedString gateName = managedString(gate);
  ui::writeField<void*>(info, SLI_SCENENAME, sceneName);
  ui::writeField<void*>(info, SLI_ENTRYGATENAME, gateName);
  pinForTravel(info, sceneName, gateName);
  call<void>(GAMEMGR_BEGINSCENETRANSITION, gm, info);
  Logging.Log("[sshud] boss rush: travel to %s @ %s", scene, gate);
  return true;
}

/// 当前场景名是不是 scene
inline bool isInScene(const char* scene) {
  void* gm = gameManager();
  if (gm == nullptr || call<bool>(GAMEMGR_GETISINSCENETRANSITION, gm)) return false;
  char current[options::NAME_CAPACITY];
  return std::strcmp(options::asciiOf(ui::readField<ui::ManagedString>(gm, GAMEMGR_SCENENAME), current, sizeof(current)),
                     scene) == 0;
}

/// "根/子/…" 对应的 GameObject：根用 GameObject.Find（须激活），其余用 Transform.Find（能找到停用的子物体）
inline void* findByPath(const char* path) {
  char root[PATH_CAPACITY];
  const char* slash = std::strchr(path, '/');
  const size_t rootLength = slash != nullptr ? static_cast<size_t>(slash - path) : std::strlen(path);
  if (rootLength >= sizeof(root)) return nullptr;
  std::memcpy(root, path, rootLength);
  root[rootLength] = '\0';
  void* rootObject = call<void*>(GO_FIND, managedString(root));
  if (!ui::isAlive(rootObject) || slash == nullptr) return ui::isAlive(rootObject) ? rootObject : nullptr;
  void* transform = call<void*>(TR_FIND, call<void*>(GO_GETTRANSFORM, rootObject), managedString(slash + 1));
  return ui::isAlive(transform) ? map::gameObjectOf(transform) : nullptr;
}

/// 执行一条到场修正；找不到物体 / 状态机返回 false
inline bool runFixup(const Fixup& fixup) {
  void* target = findByPath(fixup.path);
  if (target == nullptr) return false;
  if (fixup.kind == FixupKind::Activate || fixup.kind == FixupKind::Deactivate) {
    call<void>(GO_SETACTIVE, target, fixup.kind == FixupKind::Activate);
    return true;
  }
  void* component = fsm::componentOn(target, fixup.fsm);
  if (component == nullptr) return false;
  call<void>(fixup.kind == FixupKind::FsmEvent ? PMFSM_SENDEVENT : PMFSM_SETSTATE, component, managedString(fixup.name));
  return true;
}

}  // namespace sshud::bossrush
