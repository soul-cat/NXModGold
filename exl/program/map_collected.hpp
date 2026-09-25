// 地图收集物的"已收集"判定（CheckKind 的四种方式）。
//
// SceneData 的三个集合（persistentBools / persistentInts / geoRocks）都是
// PersistentItemDataCollection<T, ...>，其 TryGetValue 是共享泛型实现：
// 调用时必须把 metadata 槽里的 MethodInfo* 作为最后一个参数传入，槽要先用
// il2cpp_codegen_initialize_runtime_metadata 初始化——照抄游戏自己
// MapPinConditional.Condition.get_IsFulfilled 的写法（反汇编核实）。

#pragma once

#include "map_calls.hpp"
#include "map_markers_data.hpp"
#include "map_strings.hpp"

namespace sshud::map {

constexpr uintptr_t PERSISTENT_BOOL_VALUE = 0x21;  // SerializableBoolData.Value（反汇编核实）
constexpr uintptr_t PERSISTENT_INT_VALUE = 0x24;   // SerializableIntData.Value（游戏 7 处调用点核实）

inline const void* methodInfoFromSlot(uintptr_t slotRva, bool& isInitialized) {
  auto* slot = reinterpret_cast<const void**>(exl::util::modules::GetTargetOffset(slotRva));
  if (!isInitialized) {
    ui::mainFunction<void (*)(const void*, bool)>(IL2CPP_INIT_METADATA)(slot, true);
    isInitialized = true;
  }
  return *slot;
}

/// SceneData 某个集合里 (scene, id) 的记录；查不到返回 null
inline const uint8_t* findSceneRecord(uintptr_t collectionField, uintptr_t tryGetValueRva, uintptr_t slotRva,
                                      bool& isSlotInitialized, const EntryStrings& strings) {
  const void* sceneData = ui::mainFunction<const void* (*)(const void*)>(SD_GETINSTANCE)(nullptr);
  if (sceneData == nullptr) return nullptr;
  const void* collection = ui::readField<const void*>(sceneData, collectionField);
  if (collection == nullptr) return nullptr;
  const uint8_t* record = nullptr;
  const bool found = ui::mainFunction<bool (*)(const void*, ui::ManagedString, ui::ManagedString, const uint8_t**,
                                               const void*)>(tryGetValueRva)(
      collection, strings.scene, strings.persistentId, &record,
      methodInfoFromSlot(slotRva, isSlotInitialized));
  return found ? record : nullptr;
}

inline const uint8_t* findBoolRecord(const EntryStrings& strings) {
  static bool s_slotReady = false;
  return findSceneRecord(SD_PERSISTENTBOOLS, PDC_BOOL_TRYGETVALUE, PDC_BOOL_TRYGETVALUE_MI, s_slotReady, strings);
}

/// persistentInts 与 geoRocks 同类型，共用一个 MethodInfo 槽
inline const uint8_t* findIntRecord(uintptr_t collectionField, const EntryStrings& strings) {
  static bool s_slotReady = false;
  return findSceneRecord(collectionField, PDC_INT_TRYGETVALUE, PDC_INT_TRYGETVALUE_MI, s_slotReady, strings);
}

inline bool isPlayerDataFlagSet(uint32_t offset) {
  const void* playerData = ui::mainFunction<const void* (*)(const void*)>(PD_GETINSTANCE)(nullptr);
  return playerData != nullptr && ui::readField<bool>(playerData, offset);
}

/// 查不到记录一律当作未收集（没去过的场景本来就没有记录）；strings 是该条目钉住的托管字符串
inline bool isCollected(const CollectibleEntry& entry, const EntryStrings& strings) {
  switch (entry.check) {
    case CheckKind::PersistentBool: {
      const uint8_t* record = findBoolRecord(strings);
      return record != nullptr && record[PERSISTENT_BOOL_VALUE] != 0;
    }
    case CheckKind::PersistentIntAtLeast: {
      const uint8_t* record = findIntRecord(SD_PERSISTENTINTS, strings);
      return record != nullptr && ui::readField<int32_t>(record, PERSISTENT_INT_VALUE) >= entry.threshold;
    }
    case CheckKind::GeoRockDepleted: {
      const uint8_t* record = findIntRecord(SD_GEOROCKS, strings);
      return record != nullptr && ui::readField<int32_t>(record, PERSISTENT_INT_VALUE) <= 0;
    }
    case CheckKind::PlayerDataBool:
      return isPlayerDataFlagSet(entry.playerDataOffset);
  }
  return false;
}

}  // namespace sshud::map
