// BOSS 挑战用到的游戏调用：切场景、改 PlayerData 标记、到场后激活物体、锁操作、回满状态。
//
// 切场景照游戏自己的写法（HeroController.HazardRespawn 反汇编）：object_new(SceneLoadInfo) → .ctor →
// 填 SceneName / EntryGateName → GameManager.BeginSceneTransition。其余字段保持构造默认值，由游戏自己淡出淡入。
// 字符串是直接写进托管字段的（没有写屏障），和 SceneLoadInfo 一起钉住，下一次传送时再放开。
// 全部在 Unity 主线程（CameraController.LateUpdate）。

#pragma once

#include "boss_rush_table.hpp"
#include "map_calls.hpp"
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
