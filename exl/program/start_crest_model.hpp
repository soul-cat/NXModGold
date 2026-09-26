// 新存档起始纹章的纯数据（不依赖 Unity / nn）。
//
// 纹章 ID 是游戏内部名（ToolCrest 资源名，PlayerData.CurrentCrestID 与 ToolEquips 的键都用它），
// 与 PC 版 StartCrestSelector 的 trueCrestNames 同序；下标即 config.ini 里的 start_crest。
// 选项页显示的名字在运行时从 ToolCrest.displayName 取（跟随游戏语言），取不到才显示 ID。

#pragma once

#include "toggles_model.hpp"

#include <cstddef>
#include <cstdint>

namespace sshud::crest {

constexpr size_t CREST_COUNT = 9;
constexpr int32_t HUNTER_INDEX = 0;  // 游戏原样的起始纹章

constexpr const char* CREST_IDS[CREST_COUNT] = {
    "Hunter",      // 猎人
    "Reaper",      // 收割者
    "Wanderer",    // 流浪者
    "Warrior",     // 野兽
    "Witch",       // 女巫
    "Toolmaster",  // 建筑师
    "Spell",       // 萨满
    "Cursed",      // 诅咒
    "Cloakless",   // 无斗篷
};
static_assert(CREST_COUNT == static_cast<size_t>(toggles::START_CREST_COUNT), "config.ini 的 start_crest 范围须与 CREST_IDS 一致");

inline const char* crestIdOf(int32_t index) {
  return index >= 0 && index < static_cast<int32_t>(CREST_COUNT) ? CREST_IDS[index] : CREST_IDS[HUNTER_INDEX];
}

}  // namespace sshud::crest
