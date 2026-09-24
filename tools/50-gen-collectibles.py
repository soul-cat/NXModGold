#!/usr/bin/env python3
"""从 Completionist Map 的离线数据生成地图收集物表 exl/program/collectibles.gen.h。

Completionist Map（Yoraiz0r，Thunderstore）没有 license 声明，所以它的数据和由此
导出的表都不进版本库：用户自己下载该 mod 包，本脚本在本地生成头文件（已 gitignore），
与 offsets.h 的做法一致。

数据含义（docs/tasks/research-collectible-map.md 第 2 节）：
- persistent_bools.json：每条 = 场景 + transform 链 + 持久布尔 Id（= GameObject 名）。
  世界坐标 = 各级 LocalPosition 直接相加（Completionist 同款，忽略旋转缩放）。
- persistent_scenes.json：场景 tilemap 尺寸，GameMap.GetMapPosition 需要它。

用法:
    ./tools/50-gen-collectibles.py                       # 默认读 work/completionist/*.zip
    ./tools/50-gen-collectibles.py --source <zip 或解压目录>
    ./tools/50-gen-collectibles.py selftest
"""

from __future__ import annotations

import argparse
import json
import sys
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SOURCE_DIR = REPO_ROOT / "work" / "completionist"
DEFAULT_OUT = REPO_ROOT / "exl" / "program" / "collectibles.gen.h"
DATA_PREFIX = "CompletionistMap/"

# 持久布尔 Id → 分类（顺序即 map_markers.hpp 里 CollectibleKind 的枚举值）
KINDS: dict[str, str] = {
    "Heart Piece": "MaskShard",
    "Silk Spool": "SpoolFragment",
    "Collectable Item Pickup - Tool Metal": "Craftmetal",
}


@dataclass(frozen=True)
class Entry:
    scene: str
    persistent_id: str
    kind: str
    x: float
    y: float
    scene_width: int
    scene_height: int


def load_json(source: Path, name: str) -> object:
    """source 可以是 mod 的 zip，也可以是解压后的目录。"""
    if source.is_file():
        with zipfile.ZipFile(source) as archive:
            return json.loads(archive.read(DATA_PREFIX + name))
    return json.loads((source / name).read_text(encoding="utf-8"))


def world_position(transforms: list[dict]) -> tuple[float, float]:
    x = sum(t["LocalPosition"]["X"] for t in transforms)
    y = sum(t["LocalPosition"]["Y"] for t in transforms)
    return x, y


def collect_entries(bools: list[dict], scenes: dict[str, dict]) -> tuple[list[Entry], list[str]]:
    """返回 (条目, 跳过原因)。缺场景尺寸的条目无法定位，跳过并报告。"""
    entries: list[Entry] = []
    skipped: list[str] = []
    for record in bools:
        kind = KINDS.get(record["Id"])
        if kind is None:
            continue
        hierarchy = record["Hierarchy"]
        size = scenes.get(hierarchy["ActualScenePath"])
        if size is None:
            skipped.append(f"{hierarchy['CleanScenePath']}/{record['Id']}：缺场景尺寸")
            continue
        x, y = world_position(hierarchy["Transforms"])
        entries.append(
            Entry(hierarchy["CleanScenePath"], record["Id"], kind, x, y, size["Width"], size["Height"])
        )
    entries.sort(key=lambda e: (e.kind, e.scene, e.x, e.y))
    return entries, skipped


def c_string(text: str) -> str:
    return json.dumps(text, ensure_ascii=True)


def render_header(entries: list[Entry], source_name: str) -> str:
    rows = [
        f"    {{{c_string(e.scene)}, {c_string(e.persistent_id)}, CollectibleKind::{e.kind}, "
        f"{e.x:.3f}f, {e.y:.3f}f, {e.scene_width}.0f, {e.scene_height}.0f}},"
        for e in entries
    ]
    return "\n".join(
        [
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
            *rows,
            "};",
            "",
            "}  // namespace sshud::map",
            "",
        ]
    )


def find_source(explicit: str | None) -> Path | None:
    if explicit:
        return Path(explicit)
    candidates = sorted(DEFAULT_SOURCE_DIR.glob("*.zip"))
    return candidates[-1] if candidates else None


def generate(source: Path, out: Path) -> int:
    bools = load_json(source, "persistent_bools.json")
    scenes = load_json(source, "persistent_scenes.json")
    entries, skipped = collect_entries(bools, scenes)
    out.write_text(render_header(entries, source.name), encoding="utf-8")
    print(f"[✓] {out}：{len(entries)} 条")
    for kind in KINDS.values():
        print(f"    {kind:<14} {sum(1 for e in entries if e.kind == kind)}")
    for reason in skipped:
        print(f"[!] 跳过 {reason}")
    return 0


def selftest() -> int:
    """合成两条数据：一条可用、一条缺场景尺寸，核对坐标相加与输出格式。"""
    bools = [
        {"Id": "Heart Piece", "Hierarchy": {"CleanScenePath": "Crawl_02", "ActualScenePath": "A/Crawl_02.unity",
         "Transforms": [{"LocalPosition": {"X": 20.0, "Y": 4.5}}, {"LocalPosition": {"X": 2.25, "Y": 0.5}}]}},
        {"Id": "Silk Spool", "Hierarchy": {"CleanScenePath": "Nowhere", "ActualScenePath": "A/Nowhere.unity",
         "Transforms": [{"LocalPosition": {"X": 1.0, "Y": 1.0}}]}},
        {"Id": "Breakable Wall", "Hierarchy": {"CleanScenePath": "Crawl_02", "ActualScenePath": "A/Crawl_02.unity",
         "Transforms": [{"LocalPosition": {"X": 1.0, "Y": 1.0}}]}},
    ]
    scenes = {"A/Crawl_02.unity": {"Width": 30, "Height": 150}}
    entries, skipped = collect_entries(bools, scenes)
    checks = [
        (len(entries) == 1, "只应收录 1 条"),
        (entries and entries[0].x == 22.25 and entries[0].y == 5.0, "坐标应为各级相加"),
        (len(skipped) == 1, "缺尺寸的应被跳过"),
    ]
    header = render_header(entries, "fixture.zip")
    checks.append(('{"Crawl_02", "Heart Piece", CollectibleKind::MaskShard, 22.250f, 5.000f, 30.0f, 150.0f},' in header,
                   "输出行格式"))
    failed = [msg for ok, msg in checks if not ok]
    for msg in failed:
        print(f"  FAIL {msg}")
    print("50-gen-collectibles 自检：" + ("全部通过" if not failed else f"{len(failed)} 项失败"))
    return 1 if failed else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", nargs="?", choices=["generate", "selftest"], default="generate")
    parser.add_argument("--source", help="Completionist Map 的 zip 或解压目录（默认 work/completionist/*.zip）")
    parser.add_argument("--out", default=str(DEFAULT_OUT))
    args = parser.parse_args()
    if args.command == "selftest":
        return selftest()
    source = find_source(args.source)
    if source is None or not source.exists():
        print("[x] 找不到 Completionist Map 数据。从 Thunderstore 下载 Yoraiz0r/Completionist_Map 的包，", file=sys.stderr)
        print(f"    放到 {DEFAULT_SOURCE_DIR}/，或用 --source 指定。", file=sys.stderr)
        return 1
    return generate(source, Path(args.out))


if __name__ == "__main__":
    sys.exit(main())
