// 美食家任务（Great Gourmand，圣堡 Song_09b 的美食家仆人）免送肉。
//
// 任务要交五样：炖菜、花蜜、蛋、肉、珊瑚，各对应 PlayerData.GourmandGiven*。肉要从钟心镇信使处领一袋
// 一路送过去，挨打就坏。开关（toggles::Feature::SkipGourmandMeat，只在原生选项页里）打开时，
// 任务已接且肉还没交，就把 GourmandGivenMeat 置 true；其余四样照常交，奖励由游戏按正常流程发。
// 只写这一个值，不碰任务本身与物品，所以不会重复给奖励。每 CHECK_INTERVAL_SECONDS 查一次（主线程）。

#pragma once

#include "lib.hpp"
#include "map_calls.hpp"
#include "toggles.hpp"

namespace sshud::gourmand {

constexpr float CHECK_INTERVAL_SECONDS = 2.0f;

inline void applyIfDue(float now) {
  static float s_nextCheck = 0.0f;
  if (now < s_nextCheck) return;
  s_nextCheck = now + CHECK_INTERVAL_SECONDS;
  if (!toggles::g_settings.isEnabled(toggles::Feature::SkipGourmandMeat)) return;
  void* playerData = map::call<void*>(PD_GETINSTANCE);
  if (playerData == nullptr || unity::readField<bool>(playerData, PD_GOURMANDGIVENMEAT)) return;
  if (!map::call<bool>(PD_GOURMANDQUESTACCEPTED, playerData)) return;
  unity::writeField<bool>(playerData, PD_GOURMANDGIVENMEAT, true);
  Logging.Log("[sshud] gourmand: quest accepted, meat marked as delivered");
}

}  // namespace sshud::gourmand
