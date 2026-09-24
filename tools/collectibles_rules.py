"""收集物分类规则：移植 Completionist Map 对 persistent_bools.json 的处理（ProcessAll）与物品类型判定。

对应源码：PersistentBoolItemMapIcons.cs（FixTypos / AssignUseless / AssignStyles / GetItem /
GetFilterFromItemType）、PersistentBoolItemMapIcon.cs（HiddenFromCount、permadeath 可见性）。
其它数据源（item_locations / 商店 / 跳蚤 / 念珠……）在 collectibles_sources.py。
"""

from __future__ import annotations

from dataclasses import dataclass

import collectibles_tables as tables

# 与 exl/program/map_markers_data.hpp 的 CollectibleKind / CheckKind 同序（selftest 会核对）
KIND_NAMES: tuple[str, ...] = (
    "MaskShard", "SpoolFragment", "Craftmetal", "Tool", "ToolkitUpgrade", "Materium", "Relic", "Memento",
    "MemoryLocket", "Collectable", "Rosary", "ShellShard", "MossBerry", "SilkEater", "Crest", "SilkHeart",
    "Ability", "Flea",
)
CHECK_NAMES: tuple[str, ...] = ("PersistentBool", "PersistentIntAtLeast", "GeoRockDepleted", "PlayerDataBool")

# 念珠 / 贝壳碎片这类"货币"物品：商店与 item_locations 里 Completionist 会 HidesSelf
CURRENCY_KINDS: frozenset[str] = frozenset({"Rosary", "ShellShard"})
# item_locations.json 的 sheetName → 分类（Completionist 运行时查 ToolItemManager / CollectableRelicManager，
# 离线只能靠本地化表名近似）
SHEET_KINDS: dict[str, str] = {"Tools": "Tool", "CollectableRelic": "Relic"}
# 按物品名直接定类（GetFilterFor 的覆盖 + 离线补充）
NAMED_ITEM_KINDS: dict[str, str] = {
    "Heart Piece": "MaskShard",
    "Silk Spool": "SpoolFragment",
    "Silk Grub": "SilkEater",
    "Tool Metal": "Craftmetal",
    "Crest Socket Unlocker": "MemoryLocket",
    "Mossberry": "MossBerry",
    "Great Shard": "ShellShard",  # 离线补充：大块贝壳碎片（未验证是否为 CollectableItem）
}


@dataclass(frozen=True)
class Candidate:
    """一个待落表的收集物（尚未查场景尺寸 / PlayerData 偏移）。"""

    scene: str          # CleanScenePath（定位 + SceneData 查询用）
    scene_path: str     # ActualScenePath（查 persistent_scenes.json 尺寸用）
    persistent_id: str  # SceneData Id；PlayerDataBool 时为字段名
    kind: str
    check: str
    threshold: int
    x: float
    y: float
    anchor: str         # "ActualScenePath|首个 transform 名"，用于 item_locations 顶替同物体的持久布尔
    source: str
    icon: str = ""      # 数据自带的贴图名（TextureAssetPath.SpriteAssetName），没有为空


def ci_contains(text: str, pattern: str) -> bool:
    return pattern.lower() in text.lower()


def world_position(hierarchy: dict) -> tuple[float, float]:
    """Completionist 的 WorldPosition：各级 LocalPosition 直接相加（忽略旋转缩放）。"""
    transforms = hierarchy["Transforms"]
    return sum(t["LocalPosition"]["X"] for t in transforms), sum(t["LocalPosition"]["Y"] for t in transforms)


def first_name(hierarchy: dict) -> str:
    return hierarchy["Transforms"][0]["Controls"]["Name"]


def make_candidate(hierarchy: dict, persistent_id: str, kind: str, check: str, **extra: object) -> Candidate:
    """extra 可带 scene（覆盖 CleanScenePath）、threshold、source、icon。"""
    x, y = world_position(hierarchy)
    return Candidate(
        scene=str(extra.get("scene") or hierarchy["CleanScenePath"]),
        scene_path=hierarchy["ActualScenePath"],
        persistent_id=persistent_id,
        kind=kind,
        check=check,
        threshold=int(extra.get("threshold", 0)),
        x=x,
        y=y,
        anchor=f"{hierarchy['ActualScenePath']}|{first_name(hierarchy)}",
        source=str(extra.get("source", "")),
        icon=str(extra.get("icon") or ""),
    )


def sprite_name(asset_path: dict | None) -> str:
    """TextureAssetPath / SpriteTextureName 里的贴图名（Completionist 的 AssetBundleBasedIconStyle 按它找 Sprite）。"""
    return (asset_path or {}).get("SpriteAssetName") or ""


def icon_for(candidate: Candidate) -> tuple[str, str, float]:
    """(IconSource, 名字, 缩放)。顺序同 PersistentBoolItemMapIcon：分类强制图标 > 数据自带贴图 > 分类图标。"""
    if candidate.icon and candidate.kind not in tables.ICON_OVERRIDE_KINDS:
        return "SpriteName", candidate.icon, tables.ENTRY_ICON_SCALE
    return tables.KIND_ICONS[candidate.kind]


def item_kind(item_id: str, sheets: dict[str, str]) -> str:
    """GetItem + GetFilterFromItemType 的离线版。

    Completionist 运行时查 MementoList / MateriumItemManager / CollectableItemManager / ToolItemManager /
    CollectableRelicManager。离线没有这些表：纪念品用名字含 "Memento"（与 CreateItemLocationIcons 同一启发式），
    工具 / 遗物用 item_locations 的 sheetName。Materium 在 Completionist 里本来就显示为普通物品（PickupItem），
    离线无从区分，归入 Collectable。
    """
    if not item_id:
        return "Collectable"
    lowered = item_id.lower()
    if "rosary_set" in lowered or "money reward" in lowered:
        return "Rosary"
    if "tool pouch pickup" in lowered or "tool kit pickup" in lowered:
        return "ToolkitUpgrade"
    if "shard pouch" in lowered:
        return "ShellShard"
    if item_id in NAMED_ITEM_KINDS:
        return NAMED_ITEM_KINDS[item_id]
    if "memento" in lowered:
        return "Memento"
    return SHEET_KINDS.get(sheets.get(item_id, ""), "Collectable")


def is_steel_soul_only(hierarchy: dict) -> bool:
    """TryFindingCustomVisibilityTest：permadeathMode 测试的 SetOn 在链上 → 只在钢魂模式出现。"""
    names = {t["Controls"]["Name"] for t in hierarchy["Transforms"]}
    for transform in hierarchy["Transforms"]:
        for test in transform["Controls"].get("PlayerDataTests") or []:
            if test.get("FieldName") != "permadeathMode":
                continue
            if test.get("SetOn") in names:
                return True
            if test.get("SetOff") in names:
                return False
    return False


# ---- persistent_bools.json（PersistentBoolItemMapIcons.ProcessAll）----


def fix_typos(record: dict) -> dict:
    """FixTypos：返回修正后的浅拷贝（Bone_East_10_Church 的 Id/场景互换 + 场景名拼写）。"""
    hierarchy = dict(record["Hierarchy"])
    fixed = {**record, "Hierarchy": hierarchy}
    church = "Bone_East_10_Church"
    if fixed["Id"] == church:
        fixed["Id"] = hierarchy["CleanScenePath"]
        hierarchy["CleanScenePath"] = church
    hierarchy["CleanScenePath"] = tables.SCENE_TYPO_CORRECTIONS.get(hierarchy["CleanScenePath"],
                                                                    hierarchy["CleanScenePath"])
    return fixed


def _cull_control(obj_id: str, details: str) -> bool:
    if not details.lower().endswith("control"):
        return False
    return any(ci_contains(obj_id, p) for p in tables.CULL_CONTROL_ID_SUBSTRINGS)


def _useless_by_pattern(obj_id: str, details: str) -> bool:
    return (
        details in tables.USELESS_DETAILS
        or _cull_control(obj_id, details)
        or any(ci_contains(obj_id, p) for p in tables.USELESS_ID_SUBSTRINGS)
        or ci_contains(details, "Breakable Restaurant Sack")
        or obj_id in tables.USELESS_ID_EXACT
    )


def _useless_by_context(obj_id: str, details: str, hierarchy: dict) -> bool:
    """AssignUseless 里依赖场景 / 层级 / 启用状态的个案。"""
    scene, actual = hierarchy["CleanScenePath"], hierarchy["ActualScenePath"]
    transforms = hierarchy["Transforms"]
    enabled = transforms[0]["Controls"]["Enabled"]
    second = transforms[1]["Controls"]["Name"] if len(transforms) > 1 else ""
    third = transforms[2]["Controls"]["Name"] if len(transforms) > 2 else ""
    return (
        (scene == "Hang_04" and (ci_contains(obj_id, "smash_floor") or ci_contains(obj_id, "Start Range")))
        or (obj_id == "Nectar Pickup" and not enabled)
        or (details == "Ward Boss Key" and second == "Return Corpse")
        or (obj_id == "Stalactite Group Bottom" and hierarchy.get("BundleName") == "coral_35.bundle")
        or (scene == "Bone_East_10_Church" and obj_id == "Song_lever_side" and actual.endswith("10.unity"))
        or (scene == "Bone_18" and obj_id == "Collectable Item Pickup" and third == "Black Thread World")
        or (scene == "Song_Enclave" and details == "Ward Key")
        or (obj_id.startswith("Geo Small Persistent") and not enabled)
        or (scene == "Song_04" and actual.endswith("Song_03.unity"))
        or (scene == "Under_07c" and actual.endswith("Under_07b.unity"))
    )


def _useless_fsm(obj_id: str, details: str) -> bool:
    if details == "FSM" and (ci_contains(obj_id, "craw_court_summons_pin")
                             or ci_contains(obj_id, "thread_memory_orb_source")):
        return True
    if details == "Phase Control" and ci_contains(obj_id, "Blue Assistant"):
        return True
    return (details == "FSM" and obj_id not in tables.FSM_DETAIL_KEEP_IDS
            and not ci_contains(obj_id, "Black_Thread_Core"))


def is_useless(record: dict) -> bool:
    """AssignUseless（Useless 与 UselessSelf 都算：两者在地图上都不显示）+ Inaccessible。"""
    obj_id, details, hierarchy = record["Id"], record["Details"], record["Hierarchy"]
    return (
        _useless_by_pattern(obj_id, details)
        or _useless_by_context(obj_id, details, hierarchy)
        or _useless_fsm(obj_id, details)
        or (hierarchy["CleanScenePath"], obj_id) in tables.INACCESSIBLE
        or obj_id in tables.HIDDEN_SELF_IDS
        or details in tables.HIDDEN_SELF_DETAILS
    )


def is_hidden_from_count(record: dict) -> bool:
    """HiddenFromCount：Completionist 仍显示但不计数（多为同一物品的另一份摆放）。"""
    obj_id, details = record["Id"], record["Details"]
    return (
        (record["Hierarchy"]["CleanScenePath"] == "Weave_14" and details == "Silk Snare")
        or (obj_id == "Heart Piece" and details == "Deparent")
        or obj_id == "library_glass_heart_piece"
    )


def _rule_matches(rule: tuple[str, str, str, str], record: dict) -> bool:
    _, field, mode, pattern = rule
    text = record[field]
    if mode == "prefix":
        return text.lower().startswith(pattern.lower())
    if mode == "eq":
        return text == pattern
    if mode == "wall_control":
        return ci_contains(text, pattern) and ci_contains(record["Details"], "Control")
    if mode == "cog10":
        return ci_contains(text, pattern) and record["Hierarchy"]["CleanScenePath"] == "Cog_10"
    return ci_contains(text, pattern)


def style_filter(record: dict) -> str | None:
    """AssignStyles：返回 Completionist 分类名（后匹配覆盖先匹配），无匹配为 None。"""
    matched = None
    for rule in tables.STYLE_RULES:
        if _rule_matches(rule, record):
            matched = rule[0]
    return matched


def bool_record_kind(record: dict, sheets: dict[str, str]) -> str | None:
    """持久布尔条目的收集物分类；不是收集物返回 None。

    与原版的差异：拾取物按 ActualItemId 定类（为空再用 Details）。原版用 Details，
    遇到 Details 是 "Deparent" / "Silent when hero below" 这类说明文字时会落到通用物品。
    """
    matched = style_filter(record)
    if matched == "Pickup":
        return item_kind(record.get("ActualItemId") or record["Details"], sheets)
    return tables.FILTER_TO_KIND.get(matched or "")


def from_persistent_bools(records: list[dict], sheets: dict[str, str]) -> tuple[list[Candidate], list[Candidate]]:
    """返回 (计数条目, HiddenFromCount 条目)；后者由调用方在同场景没有同类计数条目时才保留。"""
    counted: list[Candidate] = []
    uncounted: list[Candidate] = []
    for raw in records:
        record = fix_typos(raw)
        if is_useless(record) or is_steel_soul_only(record["Hierarchy"]):
            continue
        kind = bool_record_kind(record, sheets)
        if kind is None:
            continue
        candidate = make_candidate(record["Hierarchy"], record["Id"], kind, "PersistentBool",
                                   source="persistent_bools", icon=sprite_name(record.get("TextureAssetPath")))
        (uncounted if is_hidden_from_count(record) else counted).append(candidate)
    return counted, uncounted
