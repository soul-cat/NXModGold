// 游戏选项页的结构探查：找出原生的横向选项行（MenuOptionHorizontal）和管导航的 MenuButtonList，
// 挑出克隆模板（恰好两个选项的开/关行）与插入锚点（内容容器里最后一个激活的选项行）。
//
// 行的约定照游戏自己的代码（GameMenuOptions.ConfigureNavigation / MenuButtonList.SetupActive 的反汇编）：
// 选项组件的父节点就是"一行"，显隐整行时开关的是这个父节点。行的父节点即内容容器。
// 页面层级没有离线资源可看，这里把看到的结构全部写进日志，排错靠日志。

#pragma once

#include "options_menu_calls.hpp"

namespace sshud::options {

constexpr size_t MAX_SCANNED_OPTIONS = 16;
constexpr int32_t SCAN_MAX_DEPTH = 8;
constexpr size_t LOG_KEY_CAPACITY = 32;

inline ui::ManagedObject componentTransform(ui::ManagedObject c) { return call<ui::ManagedObject>(COMP_GETTRANSFORM, c); }

/// 选项组件所在的行（它的父节点）
inline ui::ManagedObject rowOf(ui::ManagedObject option) { return parentOf(componentTransform(option)); }

/// 深度优先收集 tr 子树里所有 typeName 组件（含 tr 自己），最多 capacity 个
inline void collectInTree(ui::ManagedObject tr, const char* typeName, int32_t depth, ui::ManagedObject* out,
                          size_t capacity, size_t& count) {
  ui::ManagedObject found = componentByName(gameObjectOf(tr), typeName);
  if (ui::isAlive(found) && count < capacity) out[count++] = found;
  if (depth >= SCAN_MAX_DEPTH) return;
  const int32_t children = childCount(tr);
  for (int32_t i = 0; i < children; ++i) collectInTree(childAt(tr, i), typeName, depth + 1, out, capacity, count);
}

inline ui::ManagedObject findInTree(ui::ManagedObject tr, const char* typeName) {
  ui::ManagedObject found = nullptr;
  size_t count = 0;
  collectInTree(tr, typeName, 0, &found, 1, count);
  return found;
}

inline const void* optionListOf(ui::ManagedObject option) { return ui::readField<const void*>(option, MOH_OPTIONLIST); }

inline uint64_t optionCountOf(ui::ManagedObject option) {
  const void* list = optionListOf(option);
  return list == nullptr ? 0 : map::arrayLength(list);
}

/// 第 i 个选项的键名（ASCII）；越界写空串
inline const char* optionKey(ui::ManagedObject option, uint64_t i, char* out, size_t capacity) {
  const void* list = optionListOf(option);
  if (list == nullptr || i >= map::arrayLength(list)) {
    out[0] = '\0';
    return out;
  }
  return asciiOf(map::arrayItem(list, i), out, capacity);
}

inline int32_t settingTypeOf(ui::ManagedObject option) {
  ui::ManagedObject setting = ui::readField<ui::ManagedObject>(option, MOH_MENUSETTING);
  return ui::isAlive(setting) ? ui::readField<int32_t>(setting, MSET_SETTINGTYPE) : -1;
}

inline void logOption(size_t i, ui::ManagedObject option) {
  char row[NAME_CAPACITY];
  char key0[LOG_KEY_CAPACITY];
  char key1[LOG_KEY_CAPACITY];
  ui::ManagedObject rowTr = rowOf(option);
  Logging.Log("[sshud] options: opt#%zu row=%s active=%d sib=%d n=%llu keys=%s,%s apply=%d setting=%d", i,
              nameOf(rowTr, row, sizeof(row)), isActiveSelf(gameObjectOf(rowTr)) ? 1 : 0, siblingIndexOf(rowTr),
              static_cast<unsigned long long>(optionCountOf(option)), optionKey(option, 0, key0, sizeof(key0)),
              optionKey(option, 1, key1, sizeof(key1)), ui::readField<int32_t>(option, MOH_APPLYSETTINGON),
              settingTypeOf(option));
}

/// 页面上的原生选项行
struct ScreenScan {
  ui::ManagedObject options[MAX_SCANNED_OPTIONS] = {};
  size_t optionCount = 0;
  ui::ManagedObject list = nullptr;
};

inline ScreenScan scanScreen(ui::ManagedObject screenTr) {
  ScreenScan scan;
  collectInTree(screenTr, "MenuOptionHorizontal", 0, scan.options, MAX_SCANNED_OPTIONS, scan.optionCount);
  scan.list = findInTree(screenTr, "MenuButtonList");
  char listName[NAME_CAPACITY];
  Logging.Log("[sshud] options: scan found %zu options, list=%s", scan.optionCount,
              scan.list != nullptr ? nameOf(componentTransform(scan.list), listName, sizeof(listName)) : "(none)");
  for (size_t i = 0; i < scan.optionCount; ++i) logOption(i, scan.options[i]);
  return scan;
}

/// 克隆模板：最后一个恰好两个选项的行（开 / 关）
inline ui::ManagedObject chooseTemplate(const ScreenScan& scan) {
  for (size_t i = scan.optionCount; i > 0; --i) {
    ui::ManagedObject option = scan.options[i - 1];
    if (optionCountOf(option) == TOGGLE_OPTION_COUNT && rowOf(option) != nullptr) return option;
  }
  return nullptr;
}

/// 内容容器里激活的选项行中，兄弟序号最大、且小于 below 的那一行（below < 0 表示不限）
inline ui::ManagedObject lastActiveRow(const ScreenScan& scan, ui::ManagedObject content, int32_t below) {
  ui::ManagedObject best = nullptr;
  int32_t bestIndex = -1;
  for (size_t i = 0; i < scan.optionCount; ++i) {
    ui::ManagedObject row = rowOf(scan.options[i]);
    if (row == nullptr || parentOf(row) != content || !isActiveSelf(gameObjectOf(row))) continue;
    const int32_t index = siblingIndexOf(row);
    if (index > bestIndex && (below < 0 || index < below)) {
      best = row;
      bestIndex = index;
    }
  }
  return best;
}

}  // namespace sshud::options
