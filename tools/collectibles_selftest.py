"""50-gen-collectibles.py 的自检：合成数据覆盖 4 种 CheckKind、物品判定、物品分类、剔除规则与输出格式。"""

from __future__ import annotations

import re
import tempfile
from pathlib import Path
from types import ModuleType

import collectibles_rules as rules
import collectibles_tables as tables

SCENE_SIZES = {"Crawl_02": (30, 150), "Crawl_09": (40, 60), "Tut_01": (80, 40), "Ward_04": (50, 50),
               "Ward_01": (60, 60), "Dust_12": (70, 30), "Arborium_08": (90, 90), "Bonetown": (120, 80),
               "Hang_01": (100, 50), "Bone_16": (60, 40), "Crawl_01": (50, 50), "Hang_10": (92, 48)}
PD_FIELDS = {"SavedFlea_Dust_12": 0x81A, "tamedGiantFlea": 0x774, "HasWardMap": 0x2F9,
             "PurchasedBonebottomHeartPiece": 0x396}
FAKE_DUMP = """// Namespace:
public class PlayerData : PlayerDataBase // TypeDefIndex: 1
{
\tpublic bool SavedFlea_Bone_06; // 0x810
\tpublic int heartPieces; // 0x78
}
"""


def _hierarchy(scene: str, *chain: tuple[str, float, float], **controls: object) -> dict:
    transforms = [
        {"LocalPosition": {"X": x, "Y": y},
         "Controls": {"Name": name, "Enabled": controls.get("enabled", True),
                      "Persistent": controls.get("persistent", True), "PlayerDataTests": controls.get("tests", [])}}
        for name, x, y in chain
    ]
    return {"CleanScenePath": scene, "ActualScenePath": f"A/{scene}.unity", "BundleName": "", "Transforms": transforms}


def _bool(obj_id: str, scene: str, item: str = "", details: str | None = None, **controls: object) -> dict:
    return {"Id": obj_id, "Details": item if details is None else details, "ActualItemId": item,
            "Hierarchy": _hierarchy(scene, (obj_id, 1.0, 2.0), **controls)}


def _persistent_bools() -> list[dict]:
    steel_only = [{"FieldName": "permadeathMode", "SetOn": "Collectable Item Pickup - Purse", "SetOff": "x"}]
    heart = _bool("Heart Piece", "Crawl_02", details="Behind Wall Control")
    heart["Hierarchy"] = _hierarchy("Crawl_02", ("Heart Piece", 20.0, 4.5), ("Parent", 2.25, 0.5))
    return [
        heart,
        _bool("Collectable Item Pickup", "Crawl_09", "Crest Socket Unlocker"),
        _bool("Collectable Item Pickup", "Tut_01", "Rosary_Set_Frayed", details="Silent when hero below"),
        _bool("Collectable Item Pickup", "Bone_16", "Straight Pin"),
        _bool("Collectable Item Pickup", "Hang_10", "Psalm Cylinder Hang", details="Deparent"),
        _bool("Breakable Wall", "Crawl_02", details="breakable_wall_v2"),
        _bool("Collectable Item Pickup", "Library_02", "Pale_Oil"),  # Inaccessible
        _bool("Collectable Item Pickup - Purse", "Crawl_01", "Dead Mans Purse", tests=steel_only),
        _bool("Silk Spool", "Nowhere", details="Bob"),  # 缺场景尺寸
    ]


def _data() -> dict[str, list | dict]:
    grub = {"ObtainType": "Grub", "ItemId": "Silk Grub", "sheetName": "UI", "linkedPDBool": None,
            "Hierarchy": _hierarchy("Ward_04", ("Silk Grub Large Cocoon", 5.0, 6.0))}
    ward_map = {"ObtainType": "Pickup", "ItemId": "Ward Map", "sheetName": "Inspect", "linkedPDBool": "HasWardMap",
                "Hierarchy": _hierarchy("Ward_01", ("Get Map Inspect", 7.0, 8.0), persistent=False)}
    pin = {"ObtainType": "Pickup", "ItemId": "Straight Pin", "sheetName": "Tools", "linkedPDBool": None,
           "Hierarchy": _hierarchy("Bone_12", ("Other Pin", 0.0, 0.0))}
    relic = {"ObtainType": "Pickup", "ItemId": "Psalm Cylinder Hang", "sheetName": "CollectableRelic",
             "linkedPDBool": None, "Hierarchy": _hierarchy("Bone_12", ("Other Relic", 0.0, 0.0))}

    def flea(go_name: str, scene: str) -> dict:
        return {"SavedItemId": "FleasCollected Target", "GoName": go_name, "ActionName": "SavedItemGetV2",
                "LinkedPDBool": "", "Hierarchy": _hierarchy(scene, (go_name, 3.0, 4.0))}

    shop_items = [{"SavedItemId": "Heart Piece", "PlayerDataBoolName": "PurchasedBonebottomHeartPiece"},
                  {"SavedItemId": "Rosary_Set_Small", "PlayerDataBoolName": ""},
                  {"SavedItemId": "Compass", "PlayerDataBoolName": ""}]
    shop = {"Id": "Bonechurch_Shop", "ShopCollectionName": "Bonebottom Peddler Stock", "Items": shop_items,
            "Hierarchy": _hierarchy("Bonetown", ("Bonechurch_Shop", 9.0, 9.0))}
    caches = [{"hitStateCount": 3, "Hierarchy": _hierarchy("Hang_01", ("rosary_string_small", 1.0, 1.0))},
              {"hitStateCount": 2, "Hierarchy": _hierarchy("Hang_01", ("rosary_string_empty", 1.0, 1.0))}]
    rock = {"hitsLeft": 0, "id": "", "sceneName": "", "Hierarchy": _hierarchy("Bone_16", ("Geo Rock 1", 4.0, 4.0))}
    return {
        "persistent_bools": _persistent_bools(),
        "persistent_scenes": {f"A/{s}.unity": {"Width": w, "Height": h} for s, (w, h) in SCENE_SIZES.items()},
        "item_locations": [grub, ward_map, pin, relic],
        "item_fsm_actions": [flea("Flea Rescue Sleeping", "Dust_12"), flea("Giant Flea", "Arborium_08")],
        "shops": [shop], "playmakers": [], "tracker_markers": [], "rosary_caches": caches,
        "geo_rocks": [rock], "breakable_misc": [], "breakable_holders": [],
    }


def _expectations() -> dict[tuple[str, str], tuple[str, str, int]]:
    """(场景, Id) → (分类, 判定, threshold/偏移)。"""
    return {
        ("Crawl_02", "Heart Piece"): ("MaskShard", "PersistentBool", 0),
        ("Crawl_09", "Collectable Item Pickup"): ("MemoryLocket", "PersistentBool", 0),
        ("Tut_01", "Collectable Item Pickup"): ("Rosary", "PersistentBool", 0),
        ("Bone_16", "Collectable Item Pickup"): ("Tool", "PersistentBool", 0),
        ("Hang_10", "Collectable Item Pickup"): ("Relic", "PersistentBool", 0),
        ("Ward_04", "Silk Grub Large Cocoon"): ("SilkEater", "PersistentBool", 0),
        ("Ward_01", "HasWardMap"): ("Collectable", "PlayerDataBool", 0x2F9),
        ("Bonetown", "PurchasedBonebottomHeartPiece"): ("MaskShard", "PlayerDataBool", 0x396),
        ("Dust_12", "SavedFlea_Dust_12"): ("Flea", "PlayerDataBool", 0x81A),
        ("Arborium_08", "tamedGiantFlea"): ("Flea", "PlayerDataBool", 0x774),
        ("Hang_01", "rosary_string_small"): ("Rosary", "PersistentIntAtLeast", 1),
        ("Bone_16", "Geo Rock 1"): ("Rosary", "GeoRockDepleted", 0),
    }


def _check_entries(gen: ModuleType) -> list[tuple[bool, str]]:
    data = _data()
    skipped: list[str] = []
    entries = gen.resolve(gen.gather_candidates(data, skipped), data["persistent_scenes"], PD_FIELDS, skipped)
    got = {(e.scene, e.persistent_id): e for e in entries}
    checks = [(len(entries) == len(_expectations()), f"条目数 {len(entries)} 应为 {len(_expectations())}")]
    for key, (kind, check, number) in _expectations().items():
        entry = got.get(key)
        value = None if entry is None else (entry.threshold if check == "PersistentIntAtLeast" else entry.pd_offset)
        ok = entry is not None and (entry.kind, entry.check) == (kind, check) and value == number
        checks.append((ok, f"{key} 应为 {kind}/{check}/{number}"))
    heart = got.get(("Crawl_02", "Heart Piece"))
    checks.append((heart is not None and (heart.x, heart.y) == (22.25, 5.0), "坐标应为各级相加"))
    items = {key: (e.item_check, e.item_name) for key, e in got.items() if e.item_check != "None"}
    checks.append((items == {("Bone_16", "Collectable Item Pickup"): ("ToolUnlocked", "Straight Pin"),
                             ("Hang_10", "Collectable Item Pickup"): ("RelicCollected", "Psalm Cylinder Hang")},
                   "只有工具 / 遗物带物品判定（名字取 ActualItemId）"))
    checks.append((any("缺场景尺寸: Nowhere/Silk Spool" in s for s in skipped), "缺尺寸的应被跳过并报告"))
    checks.append((any(s.endswith(": Compass") for s in skipped), "无 PlayerData bool 的商品应被跳过并报告"))
    row = ('{"Crawl_02", "Heart Piece", CollectibleKind::MaskShard, CheckKind::PersistentBool, 0, 0x0, '
           '22.250f, 5.000f, 30.0f, 150.0f, IconSource::SpriteName, "Inv_0030_inv_health_backboard_mask_full", '
           '0.105f, ItemCheck::None, ""},')
    checks.append((heart is not None and row in gen.render_header(entries, "fixture.zip"), "输出行格式"))
    return checks


def _enum_names(text: str, enum: str) -> tuple[str, ...]:
    body = re.search(rf"enum class {enum}\b[^{{]*\{{(.*?)\}};", text, re.S)
    return tuple(re.findall(r"^\s*([A-Za-z_]\w*)\s*,", body.group(1), re.M)) if body else ()


def _check_misc(gen: ModuleType) -> list[tuple[bool, str]]:
    checks = [(gen.c_string('a"b\\cé') == '"a\\"b\\\\c\\303\\251"', "C++ 字符串转义")]
    with tempfile.TemporaryDirectory() as tmp:
        dump = Path(tmp) / "dump.cs"
        dump.write_text(FAKE_DUMP, encoding="utf-8")
        checks.append((gen.load_player_data_bools(dump) == {"SavedFlea_Bone_06": 0x810}, "只解析 PlayerData 的 bool"))
    if gen.CONTRACT.exists():
        text = gen.CONTRACT.read_text(encoding="utf-8")
        checks.append((_enum_names(text, "CollectibleKind") == rules.KIND_NAMES, "KIND_NAMES 与契约同序"))
        checks.append((_enum_names(text, "CheckKind") == rules.CHECK_NAMES, "CHECK_NAMES 与契约同序"))
        checks.append((_enum_names(text, "ItemCheck") == rules.ITEM_CHECK_NAMES, "ITEM_CHECK_NAMES 与契约同序"))
        sources = {source for source, _, _ in tables.KIND_ICONS.values()}
        checks.append((sources <= set(_enum_names(text, "IconSource")), "KIND_ICONS 的 IconSource 都在契约里"))
    checks.append((set(tables.KIND_ICONS) == set(rules.KIND_NAMES), "每个分类都有图标"))
    return checks


def run_selftest(gen: ModuleType) -> int:
    checks = _check_entries(gen) + _check_misc(gen)
    failed = [msg for ok, msg in checks if not ok]
    for msg in failed:
        print(f"  FAIL {msg}")
    print(f"50-gen-collectibles 自检（{len(checks)} 项）：" + ("全部通过" if not failed else f"{len(failed)} 项失败"))
    return 1 if failed else 0
