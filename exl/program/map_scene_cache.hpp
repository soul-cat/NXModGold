// 收集物所在场景 → 地图块的缓存，跨刷新保留。
//
// 实机分段计时：每次刷新按名字查 322 个场景（GetMapZoneFromSceneName + GetSceneInfo）约 160 ms，
// 是开图卡顿的主要来源。场景在地图上的位置与 GameMapScene 对象在同一个 GameMap 里不会变，
// 所以只查一次；每次刷新只重读会变的两项：IsMapped（新绘制的房间）与 activeInHierarchy
// （大地图 / 快速地图激活的区域不同）。
//
// 缓存里的托管指针（GameMapScene 及其 GameObject）跨帧持有，必须钉住（本模块的静态区 GC 扫不到）；
// 标记重建（换了 GameMap 或地图重新生成）时 reset() 放开全部。
// 查不到的场景（可能是首次刷新时地图数据还没就绪）不永久记成失败：之后每次刷新再查一次，
// 最多 MAX_SCENE_LOOKUPS 次。

#pragma once

#include "map_calls.hpp"

#include <cstring>

namespace sshud::map {

constexpr int MAX_SCENE_LOOKUPS = 3;  // 每个场景在一次缓存生命期内最多按名字查几次

struct SceneInfo {
  const char* name = nullptr;
  ui::ManagedObject scene = nullptr;     // GameMapScene
  ui::ManagedObject sceneObj = nullptr;  // GameObject
  ui::PinHandle sceneHandle = 0;
  ui::PinHandle sceneObjHandle = 0;
  Vector2 scenePos{};
  bool isMapped = false;  // 游戏记为已绘制（scenesMapped）
  bool isDrawn = false;   // 地图块在层级里激活：没买区域地图时游戏不激活，已绘制的房间也不画
  uint32_t generation = 0;        // isMapped / isDrawn 是哪次刷新读的
  uint32_t lookupGeneration = 0;  // 最近一次按名字查是哪次刷新
  int lookups = 0;                // 已按名字查了几次
};

/// 放开钉住并清空查到的对象
inline void releaseScene(SceneInfo& info) {
  ui::unpinObject(info.sceneHandle);
  ui::unpinObject(info.sceneObjHandle);
  info.scene = nullptr;
  info.sceneObj = nullptr;
}

/// 按名字定位场景（慢：游戏内部按名字遍历），查到的对象钉住
inline void locateScene(SceneInfo& info, ui::ManagedObject gameMap, ui::ManagedString name, uint32_t generation) {
  releaseScene(info);
  const int32_t zone = call<int32_t>(GM_GETMAPZONEFROMSCENE, gameMap, name);
  call<void>(GM_GETSCENEINFO, gameMap, name, zone, &info.scene, &info.sceneObj, &info.scenePos);
  info.sceneHandle = ui::pinObject(info.scene);
  info.sceneObjHandle = ui::pinObject(info.sceneObj);
  info.lookupGeneration = generation;
  ++info.lookups;
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
  SceneCache() { reset(); }

  /// 每次完整刷新开头调用
  void beginRefresh() { ++generation_; }

  const SceneInfo& get(size_t index, ui::ManagedObject gameMap, const char* sceneName, ui::ManagedString sceneString) {
    if (slotOf_[index] < 0) slotOf_[index] = findOrAdd(sceneName);
    SceneInfo& info = entries_[slotOf_[index]];
    if (needsLookup(info)) locateScene(info, gameMap, sceneString, generation_);
    if (info.generation != generation_) {
      readSceneState(info);
      info.generation = generation_;
    }
    return info;
  }

  /// 标记重建时调用：放开全部钉住，整个缓存作废
  void reset() {
    for (size_t i = 0; i < count_; ++i) releaseScene(entries_[i]);
    count_ = 0;
    for (int32_t& slot : slotOf_) slot = -1;
  }

 private:
  /// 没查到（或对象已销毁）且本次刷新还没查过、次数没用完
  bool needsLookup(const SceneInfo& info) const {
    return !ui::isAlive(info.scene) && info.lookupGeneration != generation_ && info.lookups < MAX_SCENE_LOOKUPS;
  }

  int32_t findOrAdd(const char* sceneName) {
    for (size_t i = 0; i < count_; ++i) {
      if (std::strcmp(entries_[i].name, sceneName) == 0) return static_cast<int32_t>(i);
    }
    entries_[count_] = SceneInfo{};
    entries_[count_].name = sceneName;
    return static_cast<int32_t>(count_++);
  }

  SceneInfo entries_[N];
  int32_t slotOf_[N] = {};
  size_t count_ = 0;
  uint32_t generation_ = 0;
};

}  // namespace sshud::map
