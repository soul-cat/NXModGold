// BOSS 挑战用到的游戏调用：切场景、改 PlayerData 标记、到场后激活物体、锁操作、回满状态。
//
// 切场景照游戏自己的写法（HeroController.HazardRespawn 反汇编）：object_new(SceneLoadInfo) → .ctor →
// 填 SceneName / EntryGateName → GameManager.BeginSceneTransition。其余字段保持构造默认值，由游戏自己淡出淡入。
// 字符串是直接写进托管字段的（没有写屏障），和 SceneLoadInfo 一起钉住，下一次传送时再放开。
// 全部在 Unity 主线程（CameraController.LateUpdate）。

#pragma once

#include "boss_rush_table.hpp"
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

inline void applyFlags(const BossEntry& boss) {
  void* playerData = call<void*>(PD_GETINSTANCE);
  if (playerData == nullptr) return;
  for (size_t i = 0; i < boss.flagCount; ++i) {
    call<void>(PD_SETBOOL, playerData, managedString(boss.flags[i].name), boss.flags[i].value);
    Logging.Log("[sshud] boss rush: %s=%d", boss.flags[i].name, boss.flags[i].value ? 1 : 0);
  }
}

/// 场景持久标记：TryGetValue 拿到的是集合里那份数据对象（引用类型），直接改它的值。
/// 查不到（从没存过）就是默认的 false，不用管。
inline void applySceneBools(const BossEntry& boss) {
  for (size_t i = 0; i < boss.sceneBoolCount; ++i) {
    const map::EntryStrings strings{managedString(boss.scene), managedString(boss.sceneBools[i].id)};
    const uint8_t* record = map::findBoolRecord(strings);
    if (record != nullptr) ui::writeField<bool>(const_cast<uint8_t*>(record), map::PERSISTENT_BOOL_VALUE, boss.sceneBools[i].value);
    Logging.Log("[sshud] boss rush: scene bool %s/%s=%d%s", boss.scene, boss.sceneBools[i].id,
                boss.sceneBools[i].value ? 1 : 0, record != nullptr ? "" : " (no record)");
  }
}

/// 时间流速全部复位成 1（TimeManager.Reset，游戏自己的「退出到主菜单」就这么做）：
/// BOSS 战里挨打的定格 / 慢动作若在切场景时没走完，时间可能停在 0
inline void resetTime() { call<void>(TIMEMANAGER_RESET); }

/// 菜单打开和传送途中无敌（菜单打开时角色被锁住，挨打会触发定格）
inline void setInvincible(bool isInvincible) {
  void* playerData = call<void*>(PD_GETINSTANCE);
  if (playerData != nullptr) call<void>(PD_SETBOOL, playerData, managedString("isInvincible"), isInvincible);
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

/// 激活 "根/子/…"：根用 GameObject.Find（须激活），其余用 Transform.Find（能找到停用的子物体）
inline bool activatePath(const char* path) {
  char root[PATH_CAPACITY];
  const char* slash = std::strchr(path, '/');
  const size_t rootLength = slash != nullptr ? static_cast<size_t>(slash - path) : std::strlen(path);
  if (rootLength >= sizeof(root)) return false;
  std::memcpy(root, path, rootLength);
  root[rootLength] = '\0';
  void* rootObject = call<void*>(GO_FIND, managedString(root));
  if (!ui::isAlive(rootObject)) return false;
  void* transform = call<void*>(GO_GETTRANSFORM, rootObject);
  if (slash != nullptr) transform = call<void*>(TR_FIND, transform, managedString(slash + 1));
  if (!ui::isAlive(transform)) return false;
  call<void>(GO_SETACTIVE, map::gameObjectOf(transform), true);
  return true;
}

}  // namespace sshud::bossrush
