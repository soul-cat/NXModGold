#!/usr/bin/env python3
"""从 Completionist Map 的离线数据生成地图收集物表 exl/program/collectibles.gen.h。

Completionist Map（Yoraiz0r，Thunderstore）没有 license 声明，所以它的数据和由此
导出的表都不进版本库：用户自己下载该 mod 包，本脚本在本地生成头文件（已 gitignore），
与 offsets.h 的做法一致。

数据含义（docs/tasks/research-collectible-map.md 第 2 节）：
- persistent_bools.json 等：每条 = 场景 + transform 链。世界坐标 = 各级 LocalPosition 直接相加
  （Completionist 同款，忽略旋转缩放）。分类与剔除规则见 collectibles_rules.py / collectibles_sources.py。
- persistent_scenes.json：场景 tilemap 尺寸，GameMap.GetMapPosition 需要它。
- work/il2cpp/dump.cs：PlayerData 的 bool 字段偏移（PlayerDataBool 判定用）。

用法:
    ./tools/50-gen-collectibles.py                       # 默认读 work/completionist/*.zip
    ./tools/50-gen-collectibles.py --source <zip 或解压目录> --dump <dump.cs>
    ./tools/50-gen-collectibles.py selftest
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import zipfile
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

import collectibles_rules as rules
import collectibles_sources as sources

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SOURCE_DIR = REPO_ROOT / "work" / "completionist"
DEFAULT_DUMP = REPO_ROOT / "work" / "il2cpp" / "dump.cs"
DEFAULT_OUT = REPO_ROOT / "exl" / "program" / "collectibles.gen.h"
CONTRACT = REPO_ROOT / "exl" / "program" / "map_markers_data.hpp"
DATA_PREFIX = "CompletionistMap/"

RE_PD_CLASS = re.compile(r"^public class PlayerData\b")
RE_PD_BOOL = re.compile(r"^\s*public bool ([A-Za-z_][A-Za-z0-9_]*); // (0x[0-9A-Fa-f]+)\s*$")


@dataclass(frozen=True)
class Entry:
    scene: str
    persistent_id: str
    kind: str
    check: str
    threshold: int
    pd_offset: int
    x: float
    y: float
    scene_width: float
    scene_height: float
    icon_source: str
    icon_name: str
    icon_scale: float


def load_json(source: Path, name: str) -> object:
    """source 可以是 mod 的 zip，也可以是解压后的目录。"""
    if source.is_file():
        with zipfile.ZipFile(source) as archive:
            return json.loads(archive.read(DATA_PREFIX + name))
    return json.loads((source / name).read_text(encoding="utf-8"))


def load_player_data_bools(dump: Path) -> dict[str, int]:
    """dump.cs 里 `public class PlayerData` 的 bool 字段 → 偏移（只认 bool，防止把 int 当 bool 读）。"""
    fields: dict[str, int] = {}
    inside = False
    with dump.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if not inside:
                inside = RE_PD_CLASS.match(line) is not None
                continue
            if line.startswith("}"):
                break
            match = RE_PD_BOOL.match(line)
            if match is not None:
                fields[match.group(1)] = int(match.group(2), 16)
    return fields


def gather_candidates(data: dict[str, list], skipped: list[str]) -> list[rules.Candidate]:
    """按 Completionist 的来源顺序汇总。靠前的在去重时优先保留。"""
    sheets = sources.item_sheets(data["item_locations"])
    locations = sources.from_item_locations(data["item_locations"], sheets, skipped)
    shadowed = {c.anchor for c in locations}  # Deduplicate：同一物体以 item_locations 为准
    counted, uncounted = rules.from_persistent_bools(data["persistent_bools"], sheets)
    counted_spots = {(c.scene, c.kind) for c in counted}  # HiddenFromCount 的另一份摆放不重复画
    return [
        *locations,
        *(c for c in counted if c.anchor not in shadowed),
        *sources.from_fsm_actions(data["item_fsm_actions"], sheets, skipped),
        *sources.from_shops(data["shops"], sheets, skipped),
        *sources.from_flag_spots(data["playmakers"], skipped),
        *sources.from_silk_hearts(data["tracker_markers"], skipped),
        *sources.from_fleas(data["item_fsm_actions"]),
        *sources.from_rosary_caches(data["rosary_caches"]),
        *sources.from_geo_rocks(data["geo_rocks"]),
        *sources.from_breakable_misc(data["breakable_misc"]),
        *(c for c in uncounted if c.anchor not in shadowed and (c.scene, c.kind) not in counted_spots),
    ]


def scene_size(candidate: rules.Candidate, scenes: dict[str, dict]) -> dict | None:
    """先按 ActualScenePath 查；子场景（如 *_boss_beastfly、*_festival）退到同名主场景。"""
    size = scenes.get(candidate.scene_path)
    if size is not None:
        return size
    suffix = f"/{candidate.scene}.unity"
    return next((scenes[path] for path in sorted(scenes) if path.endswith(suffix)), None)


def resolve(candidates: list[rules.Candidate], scenes: dict[str, dict], pd_fields: dict[str, int],
            skipped: list[str]) -> list[Entry]:
    """查尺寸与偏移、按 (场景, Id, 判定) 去重（保留先出现的）、排序。"""
    entries: dict[tuple[str, str, str], Entry] = {}
    for c in candidates:
        key = (c.scene, c.persistent_id, c.check)
        if key in entries:
            continue
        size = scene_size(c, scenes)
        offset = pd_fields.get(c.persistent_id, -1) if c.check == "PlayerDataBool" else 0
        if size is None or offset < 0:
            reason = "缺场景尺寸" if size is None else "dump.cs 里没有该 PlayerData bool"
            skipped.append(f"{c.source}: {reason}: {c.scene}/{c.persistent_id}")
            continue
        entries[key] = Entry(c.scene, c.persistent_id, c.kind, c.check, c.threshold, offset, c.x, c.y,
                             float(size["Width"]), float(size["Height"]), *rules.icon_for(c))
    kind_order = {name: i for i, name in enumerate(rules.KIND_NAMES)}
    return sorted(entries.values(), key=lambda e: (kind_order[e.kind], e.scene, e.x, e.y, e.persistent_id))


def c_string(text: str) -> str:
    """C++ 字符串字面量：可打印 ASCII 原样，其余按 UTF-8 字节写八进制转义（不会吞后续字符）。"""
    parts = []
    for char in text:
        if char in '"\\':
            parts.append("\\" + char)
        elif " " <= char <= "~":
            parts.append(char)
        else:
            parts.append("".join(f"\\{byte:03o}" for byte in char.encode("utf-8")))
    return '"' + "".join(parts) + '"'


def render_row(e: Entry) -> str:
    return (
        f"    {{{c_string(e.scene)}, {c_string(e.persistent_id)}, CollectibleKind::{e.kind}, CheckKind::{e.check}, "
        f"{e.threshold}, 0x{e.pd_offset:X}, {e.x:.3f}f, {e.y:.3f}f, {e.scene_width:.1f}f, {e.scene_height:.1f}f, "
        f"IconSource::{e.icon_source}, {c_string(e.icon_name)}, {e.icon_scale:.3f}f}},"
    )


def render_header(entries: list[Entry], source_name: str) -> str:
    return "\n".join([
        "// 由 tools/50-gen-collectibles.py 自动生成 —— 不要手改，不入库。",
        f"// 数据来源：{source_name}（Completionist Map，无 license，仅本地使用）",
        "",
        "#pragma once",
        "",
        '#include "map_markers_data.hpp"',
        "",
        "namespace sshud::map {",
        "",
        "inline constexpr CollectibleEntry COLLECTIBLES[] = {",
        *(render_row(e) for e in entries),
        "};",
        "",
        "}  // namespace sshud::map",
        "",
    ])


DATA_FILES = ("persistent_bools", "persistent_scenes", "item_locations", "item_fsm_actions", "shops", "playmakers",
              "tracker_markers", "rosary_caches", "geo_rocks", "breakable_misc", "breakable_holders")


def print_report(entries: list[Entry], skipped: list[str], holders: int) -> None:
    kinds = Counter(e.kind for e in entries)
    checks = Counter(e.check for e in entries)
    for kind in rules.KIND_NAMES:
        print(f"    {kind:<15} {kinds.get(kind, 0)}")
    print("    判定: " + ", ".join(f"{name} {checks.get(name, 0)}" for name in rules.CHECK_NAMES))
    for reason, count in sorted(Counter(skipped).items()):
        print(f"[!] 跳过 ×{count} {reason}")
    print(f"[!] 跳过 ×{holders} breakable_holders（念珠/贝壳碎片容器）：persistentInts <= 0 无对应 CheckKind")


def generate(source: Path, dump: Path, out: Path) -> int:
    data = {name: load_json(source, f"{name}.json") for name in DATA_FILES}
    skipped: list[str] = []
    candidates = gather_candidates(data, skipped)
    entries = resolve(candidates, data["persistent_scenes"], load_player_data_bools(dump), skipped)
    out.write_text(render_header(entries, source.name), encoding="utf-8")
    print(f"[✓] {out}：{len(entries)} 条")
    print_report(entries, skipped, sources.count_breakable_holders(data["breakable_holders"]))
    return 0


def find_source(explicit: str | None) -> Path | None:
    if explicit:
        return Path(explicit)
    candidates = sorted(DEFAULT_SOURCE_DIR.glob("*.zip"))
    return candidates[-1] if candidates else None


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] == "selftest":
        from collectibles_selftest import run_selftest

        return run_selftest(sys.modules[__name__])
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", nargs="?", choices=["generate", "selftest"], default="generate")
    parser.add_argument("--source", help="Completionist Map 的 zip 或解压目录（默认 work/completionist/*.zip）")
    parser.add_argument("--dump", default=str(DEFAULT_DUMP), help="Il2CppDumper 的 dump.cs（取 PlayerData 偏移）")
    parser.add_argument("--out", default=str(DEFAULT_OUT))
    args = parser.parse_args()
    source = find_source(args.source)
    if source is None or not source.exists():
        print("[x] 找不到 Completionist Map 数据。从 Thunderstore 下载 Yoraiz0r/Completionist_Map 的包，", file=sys.stderr)
        print(f"    放到 {DEFAULT_SOURCE_DIR}/，或用 --source 指定。", file=sys.stderr)
        return 1
    if not Path(args.dump).exists():
        print(f"[x] 找不到 {args.dump}：先跑 ./tools/20-dump-il2cpp.sh，或用 --dump 指定。", file=sys.stderr)
        return 1
    return generate(source, Path(args.dump), Path(args.out))


if __name__ == "__main__":
    sys.exit(main())
