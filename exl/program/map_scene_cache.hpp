// 收集物所在场景 → 地图块的缓存，跨刷新保留。
//
// 实机分段计时：每次刷新按名字查 322 个场景（GetMapZoneFromSceneName + GetSceneInfo）约 160 ms，
// 是开图卡顿的主要来源。场景在地图上的位置与 GameMapScene 对象在同一个 GameMap 里不会变，
// 所以只查一次；每次刷新只重读会变的两项：IsMapped（新绘制的房间）与 activeInHierarchy
// （大地图 / 快速地图激活的区域不同）。
//
// 缓存里的托管指针（GameMapScene 及其 GameObject）归 GameMap 的层级持有，GameMap 活着就有效；
// 换了 GameMap 或标记被重建（地图重新生成）时整体作废。

#pragma once

#include "map_calls.hpp"
#include "map_refresh_log.hpp"

#include <cstring>

namespace sshud::map {

struct SceneInfo {
  const char* name = nullptr;
  ui::ManagedObject scene = nullptr;     // GameMapScene
  ui::ManagedObject sceneObj = nullptr;  // GameObject
  Vector2 scenePos{};
  bool isMapped = false;  // 游戏记为已绘制（scenesMapped）
  bool isDrawn = false;   // 地图块在层级里激活：没买区域地图时游戏不激活，已绘制的房间也不画
  uint32_t generation = 0;  // isMapped / isDrawn 是哪次刷新读的
};

/// 按名字定位场景（慢：游戏内部按名字遍历），每个场景只做一次
inline SceneInfo locateScene(ui::ManagedObject gameMap, const char* sceneName, ui::ManagedString name) {
  const int32_t zone = call<int32_t>(GM_GETMAPZONEFROMSCENE, gameMap, name);
  SceneInfo info;
  info.name = sceneName;
  call<void>(GM_GETSCENEINFO, gameMap, name, zone, &info.scene, &info.sceneObj, &info.scenePos);
  return info;
}

/// 每次刷新重读会变的两项
inline void readSceneState(SceneInfo& info) {
  info.isMapped = ui::isAlive(info.scene) && call<bool>(GMS_ISMAPPED, info.scene);
  info.isDrawn = info.isMapped && call<bool>(GO_GETACTIVEINHIERARCHY, gameObjectOf(info.scene));
}

/// N = 收集物条数：每条记下自己对应的缓存槽，之后 O(1) 取
template <size_t N>
class SceneCache {
 public:
  /// 每次刷新开头调用；GameMap 换了或标记刚重建时作废整个缓存
  void beginRefresh(ui::ManagedObject gameMap, bool isRebuilt) {
    if (gameMap != gameMap_ || isRebuilt) reset(gameMap);
    ++generation_;
  }

  const SceneInfo& get(size_t index, ui::ManagedObject gameMap, const char* sceneName, ui::ManagedString sceneString) {
    if (slotOf_[index] < 0) slotOf_[index] = findOrLocate(gameMap, sceneName, sceneString);
    SceneInfo& info = entries_[slotOf_[index]];
    if (info.generation != generation_) {
      readSceneState(info);
      info.generation = generation_;
    }
    return info;
  }

 private:
  int32_t findOrLocate(ui::ManagedObject gameMap, const char* sceneName, ui::ManagedString sceneString) {
    for (size_t i = 0; i < count_; ++i) {
      if (std::strcmp(entries_[i].name, sceneName) == 0) return static_cast<int32_t>(i);
    }
    ++g_profile.sceneMisses;
    entries_[count_] = locateScene(gameMap, sceneName, sceneString);
    return static_cast<int32_t>(count_++);
  }

  void reset(ui::ManagedObject gameMap) {
    gameMap_ = gameMap;
    count_ = 0;
    for (int32_t& slot : slotOf_) slot = -1;
  }

  ui::ManagedObject gameMap_ = nullptr;
  SceneInfo entries_[N];
  int32_t slotOf_[N] = {};
  size_t count_ = 0;
  uint32_t generation_ = 0;
};

}  // namespace sshud::map
