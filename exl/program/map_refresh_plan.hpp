// 地图标记刷新的决策（纯逻辑，tests/test_map_refresh_plan.cpp 覆盖）。
//
// 游戏里调两个挂钩函数的只有四处（xref 核实）：
//   GameMap.WorldMap        : SetupMapMarkers → EnableUnlockedAreas（开大地图）
//   GameMap.TryOpenQuickMap : EnableUnlockedAreas → SetupMapMarkers（开快速地图）
//   MapMarkerMenu.PlaceMarker / RemoveMarker : 只调 SetupMapMarkers（大地图开着时放 / 删手动标记）
// 所以每次开图 EnableUnlockedAreas 恰好调一次，且区域激活状态只有在它之后才准 → 完整刷新只放在它后面。
//
// SetupMapMarkers（反汇编核实）只遍历它自己的 spawnedMapMarkers（GameMap+0x180）：先全部 SetActive(false)，
// 再按玩家放的手动标记逐个激活并设 localPosition；不碰 markerParent 下的其它子节点，
// 所以不会改我们标记的激活状态与位置，原生记录一直可信。它之后只在以下情况干活：
//   - 标记被关（我们的开关或全局 pin 状态 None）→ 隐藏（已全隐藏时什么也不做）；
//   - 标记还没建（或已被销毁）→ 只建不刷新：大地图紧接着的 EnableUnlockedAreas 会完整刷新；
//   - 标记不是由完整刷新摆好的（上次被开关隐藏、刚建好）→ 完整刷新：
//     放 / 删手动标记时后面没有 EnableUnlockedAreas 跟上，不刷新就一直是隐藏的；
//   - 其余（上次已完整刷新）→ 跳过：地图开着时收集状态不会变。
// 快速地图反过来调，EnableUnlockedAreas 已完整刷新，mode 为 Shown，随后的 SetupMapMarkers 直接跳过。

#pragma once

#include <cstdint>

namespace sshud::map {

enum class RefreshHook : uint8_t {
  SetupMapMarkers,
  EnableUnlockedAreas,
};

/// 上次对整组标记做了什么
enum class MarkerMode : uint8_t {
  Unset,   // 刚建好（或从没建过），还没完整刷新
  Hidden,  // 被开关 / pin 状态 None 整组隐藏
  Shown,   // 完整刷新过：显隐与位置都是按当时的游戏状态设的
};

enum class RefreshAction : uint8_t {
  Skip,    // 什么也不做
  Hide,    // 整组隐藏
  Create,  // 只建标记（不查询、不定位）
  Full,    // 完整刷新（必要时先建）
};

struct RefreshInputs {
  RefreshHook hook;
  bool isEnabled;   // 我们的开关开着且全局 pin 状态不是 None
  bool hasMarkers;  // 标记已建且完好
  MarkerMode lastMode;
};

/// 这次挂钩该做什么（规则见文件头）
constexpr RefreshAction decideRefresh(const RefreshInputs& in) {
  if (!in.isEnabled) return RefreshAction::Hide;
  if (in.hook == RefreshHook::EnableUnlockedAreas) return RefreshAction::Full;
  if (!in.hasMarkers) return RefreshAction::Create;
  return in.lastMode == MarkerMode::Shown ? RefreshAction::Skip : RefreshAction::Full;
}

/// 执行完 action 后的 mode（重建会先把 mode 清成 Unset，这里传执行后的当前值）
constexpr MarkerMode modeAfter(RefreshAction action, MarkerMode current) {
  switch (action) {
    case RefreshAction::Hide:
      return MarkerMode::Hidden;
    case RefreshAction::Full:
      return MarkerMode::Shown;
    case RefreshAction::Create:
    case RefreshAction::Skip:
      return current;
  }
  return current;
}

/// 我们最后一次给某个标记设的激活状态（原生记录；重建后为 Unknown，下次必设）
enum class ActiveState : uint8_t {
  Unknown,
  Off,
  On,
};

constexpr ActiveState activeStateOf(bool isOn) { return isOn ? ActiveState::On : ActiveState::Off; }

/// 只有和上次设的不同才需要调 GameObject.SetActive
constexpr bool needsSetActive(ActiveState last, bool isOn) { return last != activeStateOf(isOn); }

}  // namespace sshud::map
