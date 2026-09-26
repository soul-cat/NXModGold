// 新存档起始纹章（照 PC 版 dpinela/Silksong.StartCrestSelector，选择在原生选项页的「新存档起始纹章」行）。
//
//   1. PlayerData.SetupNewPlayerData（后）：游戏在这里建纹章表、只解锁 "Hunter"（反汇编：
//      ToolEquips.SetData("Hunter", {IsUnlocked = true})）。选了别的纹章时，把 Hunter 改回未解锁、
//      解锁所选纹章，并把 CurrentCrestID 设成它。每次 new PlayerData 都会跑这个函数，读档时随后
//      被存档内容整体覆盖，所以只有新存档会留下改动（与 PC 版同一前提）。
//   2. GameManager.StartAct3 期间的 ToolItemManager.AutoEquip：第三幕开头游戏强制装备猎人纹章
//      （按已解锁的最高等级，都没解锁时仍装基础猎人）。Switch 版把取纹章内联进了 StartAct3，
//      只能在这段时间里拦 AutoEquip：要装的纹章没解锁时（即这个存档不是猎人开局），换成当前装备的纹章。
// 都在 Unity 主线程。

#pragma once

#include "map_collected.hpp"
#include "options_menu_calls.hpp"
#include "sd_logger.hpp"
#include "start_crest_model.hpp"
#include "toggles.hpp"

namespace sshud::crest {

namespace ui = sshud::unity;
using map::call;
using map::managedString;

/// ToolCrestsData.Data（24 字节；泛型共享实现按指针传）
struct CrestSaveData {
  bool isUnlocked;
  void* slots;  // List<SlotData>，null = 用纹章默认槽位（游戏给 Hunter 也传 null）
  bool displayNewIndicator;
};
static_assert(sizeof(CrestSaveData) == 24, "须与 ToolCrestsData.Data 同布局");

inline bool g_isInStartAct3 = false;
inline ui::PinHandle g_optionListPin = 0;  // 选项列表是我们直接写进字段的（没有写屏障），钉住防回收

/// ToolEquips.SetData(id, {isUnlocked})；泛型共享方法，MethodInfo 取自已初始化的元数据槽
inline void setCrestUnlocked(void* crests, const char* id, bool isUnlocked) {
  static bool s_isSlotReady = false;
  const void* method = map::methodInfoFromSlot(SNL_CRESTDATA_SETDATA_MI, s_isSlotReady);
  const CrestSaveData data{isUnlocked, nullptr, false};
  ui::mainFunction<void (*)(void*, ui::ManagedString, const CrestSaveData*, const void*)>(SNL_CRESTDATA_SETDATA)(
      crests, managedString(id), &data, method);
}

/// SetupNewPlayerData 之后：按设置换掉起始纹章（猎人时什么都不做）
inline void applyToNewPlayerData(void* playerData, bool addEditorOverrides) {
  sdlog::start();
  toggles::loadOnce();
  const int32_t index = toggles::g_settings.startCrest;
  if (index == HUNTER_INDEX || playerData == nullptr) return;
  void* crests = ui::readField<void*>(playerData, PD_TOOLEQUIPS);
  if (crests == nullptr) return Logging.Log("[sshud] crest: ToolEquips missing, start crest unchanged");
  setCrestUnlocked(crests, CREST_IDS[HUNTER_INDEX], false);
  setCrestUnlocked(crests, crestIdOf(index), true);
  call<void>(PD_SETSTRING, playerData, managedString("CurrentCrestID"), managedString(crestIdOf(index)));
  Logging.Log("[sshud] crest: new PlayerData starts with %s (editorOverrides=%d)", crestIdOf(index),
              addEditorOverrides ? 1 : 0);
}

/// StartAct3 里要装的纹章没解锁时，换成当前装备的纹章
inline void* act3Crest(void* crest) {
  if (!ui::isAlive(crest) || call<bool>(TOOLCREST_GETISUNLOCKED, crest)) return crest;
  void* playerData = call<void*>(PD_GETINSTANCE);
  ui::ManagedString current = playerData ? ui::readField<ui::ManagedString>(playerData, PD_CURRENTCRESTID) : nullptr;
  void* replacement = current ? call<void*>(TIM_GETCRESTBYNAME, current) : nullptr;
  char wanted[options::NAME_CAPACITY];
  char used[options::NAME_CAPACITY];
  options::nameOf(crest, wanted, sizeof(wanted));
  if (!ui::isAlive(replacement)) {
    Logging.Log("[sshud] crest: act3 wants locked %s, no current crest to keep", wanted);
    return crest;
  }
  Logging.Log("[sshud] crest: act3 wants locked %s, keeping %s", wanted, options::nameOf(replacement, used, sizeof(used)));
  return replacement;
}

/// 选项行显示名：取 ToolCrest.displayName（跟随游戏语言），纹章或文字取不到时用 ID
inline ui::ManagedString displayNameOf(size_t index) {
  void* crest = call<void*>(TIM_GETCRESTBYNAME, managedString(CREST_IDS[index]));
  ui::ManagedString text = ui::isAlive(crest)
                               ? ui::localisedToString(static_cast<const uint8_t*>(crest) + TOOLCREST_DISPLAYNAME)
                               : nullptr;
  return text != nullptr ? text : managedString(CREST_IDS[index]);
}

/// 选项列表 string[CREST_COUNT]：显示名用换行拼起来再 Split（借托管代码分配数组）
inline void* buildOptionList() {
  const char16_t separator = u'\n';
  ui::ManagedString newline = ui::charToString(separator);
  ui::ManagedString joined = displayNameOf(0);
  for (size_t i = 1; i < CREST_COUNT; ++i) joined = ui::concat(joined, newline, displayNameOf(i));
  constexpr int32_t SPLIT_OPTIONS_NONE = 0;
  return call<void*>(STRING_SPLIT_CHAR, joined, separator, SPLIT_OPTIONS_NONE);
}

/// 把克隆来的开 / 关行改成纹章选择行：换选项列表、关掉本地化；列表建不出来返回 false
inline bool configureOptionRow(void* option) {
  void* list = buildOptionList();
  const uint64_t count = list != nullptr ? map::arrayLength(list) : 0;
  if (count != CREST_COUNT) {
    Logging.Log("[sshud] crest: option list has %llu entries, crest row dropped", static_cast<unsigned long long>(count));
    return false;
  }
  ui::unpinObject(g_optionListPin);
  g_optionListPin = ui::pinObject(list);
  ui::writeField<void*>(option, MOH_OPTIONLIST, list);
  ui::writeField<bool>(option, MOH_LOCALIZETEXT, false);
  char first[options::NAME_CAPACITY];
  Logging.Log("[sshud] crest: option row ready, first=%s", options::asciiOf(map::arrayItem(list, 0), first, sizeof(first)));
  return true;
}

}  // namespace sshud::crest

// IL2CPP 签名：void (PlayerData* self, bool addEditorOverrides, const MethodInfo*)
HOOK_DEFINE_TRAMPOLINE(SetupNewPlayerDataHook) {
  static void Callback(void* self, bool addEditorOverrides, const void* method) {
    Orig(self, addEditorOverrides, method);
    sshud::crest::applyToNewPlayerData(self, addEditorOverrides);
  }
};

HOOK_DEFINE_TRAMPOLINE(StartAct3Hook) {
  static void Callback(void* self, const void* method) {
    sshud::crest::g_isInStartAct3 = true;
    Orig(self, method);
    sshud::crest::g_isInStartAct3 = false;
  }
};

// IL2CPP 签名：static void (ToolCrest* crest, bool markTemp, bool removeTools, const MethodInfo*)
HOOK_DEFINE_TRAMPOLINE(AutoEquipCrestHook) {
  static void Callback(void* crest, bool markTemp, bool removeTools, const void* method) {
    if (sshud::crest::g_isInStartAct3) crest = sshud::crest::act3Crest(crest);
    Orig(crest, markTemp, removeTools, method);
  }
};

namespace sshud::crest {

inline void installHooks() {
  SetupNewPlayerDataHook::InstallAtOffset(PD_SETUPNEWPLAYERDATA);
  StartAct3Hook::InstallAtOffset(GAMEMGR_STARTACT3);
  AutoEquipCrestHook::InstallAtOffset(TIM_AUTOEQUIPCREST);
  Logging.Log("[sshud] crest hooks installed: SetupNewPlayerData, StartAct3, AutoEquip");
}

}  // namespace sshud::crest
