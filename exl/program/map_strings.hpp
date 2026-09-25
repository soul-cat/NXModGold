// 收集物表里场景名 / 持久化 ID / 物品名的托管字符串缓存：每条第一次用时建一次，用 GCHandle 钉住长期复用。
// 以前每次刷新都给 571 条各新建 2 个字符串（每次开图刷新 2 遍，约 2300 个临时对象），
// GC 压力是开图卡顿的来源之一。IL2CPP 的 GC 不移动对象，钉住后指针一直有效。

#pragma once

#include "map_calls.hpp"
#include "map_markers_data.hpp"

namespace sshud::map {

struct EntryStrings {
  ui::ManagedString scene = nullptr;
  ui::ManagedString persistentId = nullptr;
  ui::ManagedString itemName = nullptr;  // 只有带 ItemCheck 的条目才建
};

inline ui::ManagedString pinnedString(const char* text) {
  ui::ManagedString text16 = managedString(text);
  ui::pinObject(text16);
  return text16;
}

template <size_t N>
class PinnedEntryStrings {
 public:
  const EntryStrings& get(size_t index, const CollectibleEntry& entry) {
    EntryStrings& strings = strings_[index];
    if (strings.scene == nullptr) strings.scene = pinnedString(entry.scene);
    if (strings.persistentId == nullptr) strings.persistentId = pinnedString(entry.persistentId);
    if (strings.itemName == nullptr && entry.itemCheck != ItemCheck::None) {
      strings.itemName = pinnedString(entry.itemName);
    }
    return strings;
  }

 private:
  EntryStrings strings_[N];
};

}  // namespace sshud::map
