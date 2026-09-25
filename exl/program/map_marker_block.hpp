// 我们建的一组收集物标记（每条收集物一个 GameObject）。
//
// 创建时逐个用 GCHandle 钉住（本模块的静态区 GC 扫不到），之后直接用存下的指针：
// 不再每次刷新 Find("sshud_pin_0") + 按下标 GetChild —— 那样在旧标记残留时可能对错位，
// 去动玩家自己的手动标记。重建信号：父节点换了，或任一存下的标记已被销毁（m_CachedPtr 清零）。
//
// 原生数组记下每个标记最后一次设的激活状态与是否已定位，只在变化时才调 Unity；
// 这些记录可信的前提是游戏不碰我们的标记（见 map_refresh_plan.hpp 文件头）。

#pragma once

#include "map_calls.hpp"
#include "map_refresh_plan.hpp"

namespace sshud::map {

template <size_t N>
class MarkerBlock {
 public:
  /// 已建、父节点没换、钉住的标记都没被销毁
  bool isIntact(ui::ManagedObject parent) const {
    if (!isCreated_ || parent != parent_) return false;
    for (ui::ManagedObject marker : markers_) {
      if (marker != nullptr && !ui::isAlive(marker)) return false;
    }
    return true;
  }

  /// 丢掉旧的一组（销毁残存的免得重复、放开钉住），准备在 parent 下建新的一组
  void beginBlock(ui::ManagedObject parent) {
    discard();
    parent_ = parent;
    isCreated_ = true;
  }

  /// 丢掉整组，回到"没建过"
  void discard() {
    for (size_t i = 0; i < N; ++i) release(i);
    parent_ = nullptr;
    isCreated_ = false;
    isAllHidden_ = false;
    mode_ = MarkerMode::Unset;
  }

  /// 登记第 i 个新建的标记并钉住
  void add(size_t i, ui::ManagedObject marker) {
    markers_[i] = marker;
    handles_[i] = ui::pinObject(marker);
  }

  ui::ManagedObject at(size_t i) const { return markers_[i]; }

  /// 与上次设的不同才调 SetActive
  void setMarkerActive(size_t i, bool isOn) {
    if (markers_[i] == nullptr || !needsSetActive(active_[i], isOn)) return;
    setActive(markers_[i], isOn);
    active_[i] = activeStateOf(isOn);
    if (isOn) isAllHidden_ = false;
  }

  /// 整组隐藏；已全隐藏时什么也不做
  void hideAll() {
    if (isAllHidden_) return;
    for (size_t i = 0; i < N; ++i) setMarkerActive(i, false);
    isAllHidden_ = true;
  }

  bool isPlaced(size_t i) const { return placed_[i]; }
  void setPlaced(size_t i, bool isPlaced) { placed_[i] = isPlaced; }
  MarkerMode mode() const { return mode_; }
  void setMode(MarkerMode mode) { mode_ = mode; }

 private:
  void release(size_t i) {
    if (ui::isAlive(markers_[i])) destroyObject(markers_[i]);
    ui::unpinObject(handles_[i]);
    markers_[i] = nullptr;
    active_[i] = ActiveState::Unknown;
    placed_[i] = false;
  }

  ui::ManagedObject markers_[N] = {};
  ui::PinHandle handles_[N] = {};
  ActiveState active_[N] = {};
  bool placed_[N] = {};
  ui::ManagedObject parent_ = nullptr;  // 只用来比较、从不解引用，不必钉住
  bool isCreated_ = false;
  bool isAllHidden_ = false;
  MarkerMode mode_ = MarkerMode::Unset;
};

}  // namespace sshud::map
