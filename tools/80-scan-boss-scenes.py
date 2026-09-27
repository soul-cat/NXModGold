#!/usr/bin/env python3
"""扫描 BOSS 场景资源包：场景加载 / BOSS 登场时检查了哪些 PlayerData 字段、哪些场景持久标记。

BOSS 挑战要把「已击败」之类的标记改回去，BOSS 才会重新出现。哪些标记起作用写在场景数据里：
组件（PlayerDataTest、TestGameObjectActivator…）和 PlayMaker 状态机的动作参数里出现的字符串，
只要等于某个 PlayerData 字段名，就是这个场景读写的标记。本脚本把它们连同所在 GameObject 路径列出来，
并标出该字段在参考存档（默认 100% 存档）里的值，供人工整理 BOSS 表。

资源包从 RomFS 解出（tools/ncaext --match，见 HANDOFF），放在 work/bundles/（gitignore）。
需要 UnityPy（work/pyenv）。

用法:
    work/pyenv/bin/python tools/80-scan-boss-scenes.py tut_03 shellwood_18 [--save <userN.dat>]
"""

import argparse
import json
import re
import sys
from pathlib import Path

import UnityPy

REPO = Path(__file__).resolve().parent.parent
BUNDLE_DIR = REPO / "work/bundles/Data/StreamingAssets/aa/Switch/scenes_scenes_scenes"
DUMP = REPO / "work/il2cpp/dump.cs"
UNITY_VERSION = "6000.0.50f1"  # 资源包里的版本号被抹掉了；取自 main 里的字符串
MAX_DEPTH = 40


def player_data_fields() -> dict[str, str]:
    """dump.cs 里 PlayerData 的实例字段：名字 → 类型"""
    fields: dict[str, str] = {}
    inside = False
    for line in DUMP.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("public class PlayerData :"):
            inside = True
            continue
        if inside and line.startswith("}"):
            break
        m = re.match(r"\s+public (\w+) (\w+); // 0x", line) if inside else None
        if m:
            fields[m.group(2)] = m.group(1)
    return fields


def game_object_path(go) -> str:
    """GameObject 的层级路径（根 / … / 自己）"""
    names = []
    transform = None
    for comp in go.m_Components:
        obj = comp.deref()
        if obj.type.name in ("Transform", "RectTransform"):
            transform = obj.read()
            break
    names.append(go.m_Name)
    depth = 0
    while transform is not None and depth < MAX_DEPTH:
        parent_ptr = transform.m_Father
        if parent_ptr.path_id == 0:
            break
        parent = parent_ptr.deref().read()
        names.append(parent.m_GameObject.deref().read().m_Name)
        transform = parent
        depth += 1
    return "/".join(reversed(names))


def walk_strings(node, key_path: str, out: list):
    """递归收集 typetree 里的全部字符串（key 路径, 值）"""
    if isinstance(node, str):
        out.append((key_path, node))
    elif isinstance(node, dict):
        for key, value in node.items():
            walk_strings(value, f"{key_path}.{key}" if key_path else key, out)
    elif isinstance(node, list):
        for index, value in enumerate(node[:2000]):
            walk_strings(value, f"{key_path}[{index}]", out)


def fsm_string_hits(fsm: dict, names: set[str]) -> list[tuple[str, str]]:
    """状态机动作参数里等于 PlayerData 字段名的字符串：(状态/动作, 字段名)"""
    hits = []
    for state in fsm.get("states", []):
        data = state["actionData"]
        strings = list(data.get("stringParams", []))
        strings += [v.get("value", "") for v in data.get("fsmStringParams", [])]
        raw = bytes(data.get("byteData", []))
        strings += [s.decode("utf8", "replace") for s in re.findall(rb"[A-Za-z_][A-Za-z0-9_]{3,}", raw)]
        actions = ",".join(a.split(".")[-1] for a in data.get("actionNames", []))
        for s in strings:
            if s in names:
                hits.append((f"state '{state['name']}' [{actions[:120]}]", s))
    return hits


def scan_scene(bundle: Path, names: set[str], save_values: dict) -> None:
    UnityPy.config.FALLBACK_UNITY_VERSION = UNITY_VERSION
    env = UnityPy.load(str(bundle))
    print(f"\n######## {bundle.stem}")
    seen = set()
    for obj in env.objects:
        if obj.type.name != "MonoBehaviour":
            continue
        try:
            tree = obj.read_typetree()
        except Exception:  # noqa: BLE001 - 个别组件类型树读不出，跳过
            continue
        hits = []
        if "fsm" in tree and isinstance(tree["fsm"], dict):
            hits = [(f"FSM '{tree['fsm'].get('name')}' {where}", s) for where, s in fsm_string_hits(tree["fsm"], names)]
        else:
            strings: list = []
            walk_strings({k: v for k, v in tree.items() if k not in ("m_Name",)}, "", strings)
            hits = [(f"{sorted(tree.keys())[4:9]} {where}", s) for where, s in strings if s in names]
        if not hits:
            continue
        try:
            path = game_object_path(tree_go(obj))
        except Exception:  # noqa: BLE001
            path = "?"
        for where, field in hits:
            key = (path, where, field)
            if key in seen:
                continue
            seen.add(key)
            print(f"  {field:40s} save={save_values.get(field)!s:6s} @ {path} :: {where}")


def tree_go(obj):
    return obj.read().m_GameObject.deref().read()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("scenes", nargs="+", help="场景包名（不含 .bundle，小写），可用前缀如 bone_05 匹配 bone_05*.bundle")
    parser.add_argument("--save", help="参考存档（Switch 明文 JSON），标出字段当前值")
    args = parser.parse_args()
    fields = player_data_fields()
    names = {n for n, t in fields.items() if t in ("bool", "int", "string")}
    save_values = json.loads(Path(args.save).read_text())["playerData"] if args.save else {}
    for scene in args.scenes:
        bundles = sorted(BUNDLE_DIR.glob(f"{scene.lower()}*.bundle"))
        if not bundles:
            print(f"[!] 没有 {scene} 的资源包", file=sys.stderr)
        for bundle in bundles:
            scan_scene(bundle, names, save_values)
    return 0


if __name__ == "__main__":
    sys.exit(main())
