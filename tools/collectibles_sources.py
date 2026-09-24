"""persistent_bools.json 以外的收集物来源（移植 ModMapData.LoadMapDirect 的各个分支）。

每个函数返回 Candidate 列表；无法用 4 种 CheckKind 表达的条目写进 skipped（"来源: 原因: 名称"）。
"""

from __future__ import annotations

from collections.abc import Iterator

import collectibles_tables as tables
from collectibles_rules import (CURRENCY_KINDS, Candidate, first_name, is_steel_soul_only, item_kind, make_candidate,
                                sprite_name)

UNEXPRESSIBLE = "判定只能靠运行时物品状态（ToolItem/Materium/Memento 等），4 种 CheckKind 无法表达"
KEY_ITEMS_EXCLUDED = frozenset({"Ward Key", "Simple Key", "Dock Demo Key"})


def item_sheets(locations: list[dict]) -> dict[str, str]:
    """ItemId → item_locations 的 sheetName（Tools / CollectableRelic / UI ...）。"""
    return {loc["ItemId"]: loc["sheetName"] for loc in locations}


def _location_selected(loc: dict) -> bool:
    """CreateItemLocationIcons 的筛选：冲刺奖励、地图、纪念品、NPC 检视、钥匙、丝食者。"""
    item, obtain = loc["ItemId"], loc["ObtainType"]
    controls = loc["Hierarchy"]["Transforms"][0]["Controls"]
    listed = ((obtain == "Sprint" and item != "Great Shard") or "map" in item.lower()
              or "memento" in item.lower() or obtain == "BasicNPC")
    is_key = "key" in item.lower() and item not in KEY_ITEMS_EXCLUDED
    is_grub = (item == "Silk Grub" and controls["Enabled"] and controls["Persistent"]
               and loc["Hierarchy"]["CleanScenePath"] != "Under_20")
    return listed or is_key or is_grub


def from_item_locations(locations: list[dict], sheets: dict[str, str], skipped: list[str]) -> list[Candidate]:
    """拾取点本身带持久布尔就用它（Id = 首个 transform 名），否则退到 linkedPDBool。"""
    found: list[Candidate] = []
    for loc in locations:
        hierarchy, item = loc["Hierarchy"], loc["ItemId"]
        if not _location_selected(loc) or loc["sheetName"] == "Journal" or is_steel_soul_only(hierarchy):
            continue
        kind = item_kind(item, sheets)
        if kind in CURRENCY_KINDS:
            continue
        linked = loc.get("linkedPDBool") or ""
        icon = sprite_name(loc.get("TextureAssetPath"))
        if hierarchy["Transforms"][0]["Controls"]["Persistent"]:
            found.append(make_candidate(hierarchy, first_name(hierarchy), kind, "PersistentBool",
                                        source="item_locations", icon=icon))
        elif linked:
            found.append(make_candidate(hierarchy, linked, kind, "PlayerDataBool", source="item_locations", icon=icon))
        else:
            skipped.append(f"item_locations: {UNEXPRESSIBLE}: {item}")
    return found


def _fsm_action_kept(action: dict) -> bool:
    """CreateItemFsmActionIcons 的剔除规则。"""
    item, scene = action["SavedItemId"], action["Hierarchy"]["CleanScenePath"]
    if item in tables.FSM_REMOVED_ITEMS or action["ActionName"] in tables.FSM_TAKE_ACTIONS:
        return False
    if item == "Tool Pouch Pickup" and scene == "Bone_12" and action["ActionName"] == "DialogueYesNoItemV3":
        return False
    return not (item == "Tool Pouch Pickup" and scene == "Coral_Judge_Arena")


def from_fsm_actions(actions: list[dict], sheets: dict[str, str], skipped: list[str]) -> list[Candidate]:
    """NPC / FSM 给的物品。只有带 LinkedPDBool 的（地图机）能判定；其余靠物品状态，跳过。"""
    found: list[Candidate] = []
    for action in actions:
        if not _fsm_action_kept(action):
            continue
        item = action["SavedItemId"]
        linked = action.get("LinkedPDBool") or ""
        if not linked:
            skipped.append(f"item_fsm_actions: {UNEXPRESSIBLE}: {item}")
            continue
        found.append(make_candidate(action["Hierarchy"], linked, item_kind(item, sheets), "PlayerDataBool",
                                    source="item_fsm_actions", icon=sprite_name(action.get("TextureAssetPath"))))
    return found


def from_fleas(actions: list[dict]) -> list[Candidate]:
    """跳蚤：Completionist 用游戏自带的跳蚤 pin，但 item_fsm_actions 里的 "FleasCollected Target"
    给出了每只跳蚤的位置；字段名 = SavedFlea_<场景>（游戏自己的 pin 名也是这个规则），特例见表。"""
    found: list[Candidate] = []
    for action in actions:
        if action["SavedItemId"] != tables.FLEA_ITEM_ID:
            continue
        hierarchy = action["Hierarchy"]
        field = tables.FLEA_SPECIAL_FIELDS.get(action["GoName"], f"SavedFlea_{hierarchy['CleanScenePath']}")
        found.append(make_candidate(hierarchy, field, "Flea", "PlayerDataBool", source="fleas"))
    return found


def _first_owners(owners: list[dict]) -> Iterator[dict]:
    """AddShopIcons：同一 ShopCollectionName 只取第一个店主（空名不去重）。"""
    seen: set[str] = set()
    for owner in owners:
        name = owner["ShopCollectionName"]
        if name and name in seen:
            continue
        seen.add(name)
        yield owner


def _non_grindle_bools(owners: list[dict]) -> set[str]:
    return {item["PlayerDataBoolName"] for owner in owners if owner["ShopCollectionName"] != "Grindle Stock"
            for item in owner["Items"] if item["PlayerDataBoolName"]}


def from_shops(owners: list[dict], sheets: dict[str, str], skipped: list[str]) -> list[Candidate]:
    """商店：有 PlayerDataBoolName 的商品按 PlayerDataBool 判定。货币类隐藏；Grindle 与其它店重复的
    商品原版只是不计数，这里直接去掉（它们第三幕才出现，常驻显示会误导）。"""
    found: list[Candidate] = []
    elsewhere = _non_grindle_bools(owners)
    for owner in _first_owners(owners):
        is_grindle = owner["ShopCollectionName"] == "Grindle Stock"
        for item in owner["Items"]:
            kind, flag = item_kind(item["SavedItemId"], sheets), item["PlayerDataBoolName"]
            if kind in CURRENCY_KINDS or (is_grindle and flag in elsewhere):
                continue
            if not flag:
                skipped.append(f"shops: {UNEXPRESSIBLE}: {item['SavedItemId']}")
                continue
            found.append(make_candidate(owner["Hierarchy"], flag, kind, "PlayerDataBool", source="shops",
                                        icon=sprite_name(item.get("SpriteTextureName"))))
    return found


def _find_flag(flags: list[dict], go_name: str, scene: str) -> dict | None:
    return next((f for f in flags if f["gameObjectName"] == go_name
                 and f["Hierarchy"]["CleanScenePath"] == scene), None)


def from_flag_spots(flags: list[dict], skipped: list[str]) -> list[Candidate]:
    """FindItemsByFlags 里写死的能力 / 丝技 / 丝轴（位置取 playmakers.json 的对应物体）。"""
    found: list[Candidate] = []
    for display, go_name, scene, field, kind in tables.FLAG_SPOTS:
        flag = _find_flag(flags, go_name, scene)
        if flag is None:
            skipped.append(f"playmakers: 找不到位置: {display}（{scene}/{go_name}）")
            continue
        found.append(make_candidate(flag["Hierarchy"], field, kind, "PlayerDataBool", source="playmakers"))
    return found


def from_silk_hearts(markers: list[dict], skipped: list[str]) -> list[Candidate]:
    """AddCustomIcons：tracker_markers 里第一个含 "Silk Heart" 的标记 + 击败 Boss 的 PlayerData bool。"""
    found: list[Candidate] = []
    for scene, field in tables.SILK_HEART_SPOTS:
        marker = next((m for m in markers if "Silk Heart" in m["Items"]
                       and m["Hierarchy"]["CleanScenePath"] == scene), None)
        if marker is None:
            skipped.append(f"tracker_markers: 找不到丝之心: {scene}")
            continue
        found.append(make_candidate(marker["Hierarchy"], field, "SilkHeart", "PlayerDataBool",
                                    source="tracker_markers"))
    return found


def rosary_cache_threshold(cache: dict) -> int:
    """RosaryCacheIcon.CountToReach：hitStateCount - 1，念珠串再减 1。"""
    count = cache["hitStateCount"] - 1
    if "string" in first_name(cache["Hierarchy"]) and count > 0:
        count -= 1
    return count


def from_rosary_caches(caches: list[dict]) -> list[Candidate]:
    """AddRosaryCacheIcons：名字含 "empty" 的空串与 Ant_05b 去掉；persistentInts >= CountToReach。"""
    found: list[Candidate] = []
    for cache in caches:
        hierarchy = cache["Hierarchy"]
        name = first_name(hierarchy)
        if "empty" in name or hierarchy["CleanScenePath"] == "Ant_05b":
            continue
        found.append(make_candidate(hierarchy, name, "Rosary", "PersistentIntAtLeast",
                                    threshold=rosary_cache_threshold(cache), source="rosary_caches"))
    return found


def from_geo_rocks(rocks: list[dict]) -> list[Candidate]:
    """念珠石：id / sceneName 为空时取首个 transform 名 / CleanScenePath（原版用 GetBaseSceneName）。"""
    return [
        make_candidate(rock["Hierarchy"], rock["id"] or first_name(rock["Hierarchy"]), "Rosary", "GeoRockDepleted",
                       scene=rock["sceneName"], source="geo_rocks")
        for rock in rocks
    ]


def from_breakable_misc(breakables: list[dict]) -> list[Candidate]:
    """AddBreakableMiscIcons：掉念珠的可破坏物，persistentBools[场景, 名字]。"""
    return [
        make_candidate(b["Hierarchy"], first_name(b["Hierarchy"]), "Rosary", "PersistentBool", source="breakable_misc")
        for b in breakables if b["hasRosaries"]
    ]


def count_breakable_holders(holders: list[dict]) -> int:
    """breakable_holders 用 persistentInts <= 0 判定（BreakableIntIcon），没有对应的 CheckKind。
    只统计原版会显示为念珠 / 贝壳碎片的那些，供报告用。"""
    kept = [h for h in holders if h["Hierarchy"]["CleanScenePath"] != "Shellwood_11"
            and "ant_item_string" not in first_name(h["Hierarchy"])
            and not (h["Hierarchy"]["CleanScenePath"] == "Bonetown" and "Chunk" in first_name(h["Hierarchy"]))]
    return sum(1 for h in kept if any(k in first_name(h["Hierarchy"]).lower() for k in ("rosary", "shard")))
