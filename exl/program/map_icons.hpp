// 地图标记图标：照 Completionist Map（MapStyles / AssetBundleBasedIconStyle）换成游戏自带贴图。
//
// 按名字找贴图：Resources.FindObjectsOfTypeAll(typeof(Sprite)) 扫一遍逐个比 name，
// typeof(Sprite) 取自模板标记自己那张 sprite 的 GetType()，不碰 il2cpp 反射 API。
// FindObjectsOfTypeAll 只返回已载入的资源，找不到的下次开图再试（调用方限次数）。
// 查到的 Sprite 只在本次刷新内用、放在栈上：本模块的静态区 GC 扫不到，不能跨帧持有。

#pragma once

#include "map_calls.hpp"
#include "map_markers_data.hpp"

#include <cstring>

namespace sshud::map {

constexpr uintptr_t IL2CPP_STRING_LENGTH = 0x10;
constexpr uintptr_t IL2CPP_STRING_CHARS = 0x14;
constexpr int MAX_ICON_NAMES = 96;

/// 托管字符串（UTF-16）是否等于 ASCII 串；不分配
inline bool managedEquals(ui::ManagedString text, const char* ascii) {
  if (text == nullptr) return false;
  const int32_t length = ui::readField<int32_t>(text, IL2CPP_STRING_LENGTH);
  for (int32_t i = 0; i < length; ++i) {
    const uint16_t ch = ui::readField<uint16_t>(text, IL2CPP_STRING_CHARS + i * sizeof(uint16_t));
    if (ascii[i] == '\0' || ch != static_cast<unsigned char>(ascii[i])) return false;
  }
  return ascii[length] == '\0';
}

inline ui::ManagedObject spriteOf(ui::ManagedObject go) {
  if (!ui::isAlive(go)) return nullptr;
  ui::ManagedObject renderer = componentByName(go, "SpriteRenderer");
  return ui::isAlive(renderer) ? call<ui::ManagedObject>(SR_GETSPRITE, renderer) : nullptr;
}

/// 一次扫描找齐一批贴图名
class SpriteLookup {
 public:
  void want(const char* name) {
    if (count_ < MAX_ICON_NAMES && indexOf(name) < 0) names_[count_++] = name;
  }

  void scan(ui::ManagedObject anySprite) {
    if (count_ == 0 || !ui::isAlive(anySprite)) return;
    ui::ManagedObject spriteType = call<ui::ManagedObject>(SYSOBJ_GETTYPE, anySprite);
    const void* all = call<const void*>(RES_FINDOBJECTSOFTYPEALL, spriteType);
    if (all == nullptr) return;
    const uint64_t total = arrayLength(all);
    for (uint64_t i = 0; i < total && found_ < count_; ++i) match(arrayItem(all, i));
  }

  ui::ManagedObject get(const char* name) const {
    const int i = indexOf(name);
    return i < 0 ? nullptr : sprites_[i];
  }

 private:
  void match(ui::ManagedObject sprite) {
    if (sprite == nullptr) return;
    const ui::ManagedString name = call<ui::ManagedString>(UOBJ_GETNAME, sprite);
    for (int j = 0; j < count_; ++j) {
      if (sprites_[j] == nullptr && managedEquals(name, names_[j])) {
        sprites_[j] = sprite;
        ++found_;
        return;
      }
    }
  }

  int indexOf(const char* name) const {
    for (int i = 0; i < count_; ++i) {
      if (std::strcmp(names_[i], name) == 0) return i;
    }
    return -1;
  }

  const char* names_[MAX_ICON_NAMES] = {};
  ui::ManagedObject sprites_[MAX_ICON_NAMES] = {};
  int count_ = 0;
  int found_ = 0;
};

struct Icon {
  ui::ManagedObject sprite;
  float scale;
};

/// 游戏自己的第一个跳蚤标记（fleaPinParents[0] 的第 0 个子节点，Completionist 的 CollectionPin 同款）
inline Icon fleaPinIcon(ui::ManagedObject gameMap) {
  const void* parents = ui::readField<const void*>(gameMap, GM_FLEAPINPARENTS);
  if (parents == nullptr || arrayLength(parents) == 0) return {nullptr, 1.0f};
  ui::ManagedObject parent = arrayItem(parents, 0);
  if (!ui::isAlive(parent) || childCount(parent) == 0) return {nullptr, 1.0f};
  ui::ManagedObject pin = childAt(parent, 0);
  return {spriteOf(gameObjectOf(pin)), call<ui::Vector3>(TR_GETLOCALSCALE, pin).x};
}

/// 本次刷新可用的全部图标来源
struct IconSet {
  SpriteLookup byName;
  ui::ManagedObject smallGeo = nullptr;
  Icon flea{nullptr, 1.0f};

  /// 先 byName.want(...) 登记要找的名字，再调它
  void load(ui::ManagedObject gameMap, ui::ManagedObject markerTemplate) {
    byName.scan(spriteOf(markerTemplate));
    smallGeo = spriteOf(call<ui::ManagedObject>(GAMEPLAY_GETSMALLGEOPREFAB));
    flea = fleaPinIcon(gameMap);
  }

  Icon resolve(const CollectibleEntry& entry) const {
    switch (entry.iconSource) {
      case IconSource::SpriteName:
        return {byName.get(entry.iconName), entry.iconScale};
      case IconSource::SmallGeo:
        return {smallGeo, entry.iconScale};
      case IconSource::FleaPin:
        return {flea.sprite, entry.iconScale * flea.scale};
      case IconSource::CollectableItem: {
        ui::ManagedObject item = call<ui::ManagedObject>(CIM_GETITEMBYNAME, managedString(entry.iconName));
        return {ui::isAlive(item) ? call<ui::ManagedObject>(CI_GETCOLLECTIONICON, item) : nullptr, entry.iconScale};
      }
    }
    return {nullptr, 1.0f};
  }
};

/// 给标记换上图标；拿不到贴图返回 false（标记保留染色圆点）
inline bool applyIcon(ui::ManagedObject marker, const Icon& icon) {
  if (!ui::isAlive(icon.sprite)) return false;
  ui::ManagedObject renderer = componentByName(marker, "SpriteRenderer");
  if (!ui::isAlive(renderer)) return false;
  call<void>(SR_SETSPRITE, renderer, icon.sprite);
  call<void>(SR_SETCOLOR, renderer, ui::Color{1.0f, 1.0f, 1.0f, 1.0f});
  call<void>(TR_SETLOCALSCALE, transformOf(marker), ui::Vector3{icon.scale, icon.scale, icon.scale});
  return true;
}

}  // namespace sshud::map
