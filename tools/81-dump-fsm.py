#!/usr/bin/env python3
"""把场景资源包里某些 GameObject 上的 PlayMaker 状态机解成可读文本：变量、状态、动作参数、跳转。

PlayMaker 把动作参数序列化在 actionData 里（byteData + 各类 Fsm* 参数表，按 paramDataType 解码）。
用于查 BOSS 怎么开战 / 分段 / 结束（例：苍白之母的 Phase Control、苔藓之母的 Battle Scene）。

资源包在 work/bundles/（tools/ncaext --match 解出，见 HANDOFF），需要 UnityPy（work/pyenv）。

用法:
    work/pyenv/bin/python tools/81-dump-fsm.py tut_03 "Battle Scene" "Mossbone Mother"
      场景包名（小写，不含 .bundle），之后是 GameObject 名（完全相等，可多个）
"""

import argparse
import struct
import sys
from pathlib import Path

import UnityPy

REPO = Path(__file__).resolve().parent.parent
BUNDLE_DIR = REPO / "work/bundles/Data/StreamingAssets/aa/Switch/scenes_scenes_scenes"
UNITY_VERSION = "6000.0.50f1"

# HutongGames.PlayMaker.ParamDataType（dump.cs 枚举顺序）
PARAM_TYPES = (
    "Integer Boolean Float String Color ObjectReference LayerMask Enum Vector2 Vector3 Vector4 Rect Array "
    "Character AnimationCurve FsmFloat FsmInt FsmBool FsmString FsmGameObject FsmOwnerDefault FunctionCall "
    "FsmAnimationCurve FsmEvent FsmObject FsmColor Unsupported GameObject FsmVector3 LayoutOption FsmRect "
    "FsmEventTarget FsmMaterial FsmTexture Quaternion FsmQuaternion FsmProperty FsmVector2 FsmTemplateControl "
    "FsmVar CustomClass FsmArray FsmEnum"
).split()

TABLES = {
    "FsmFloat": "fsmFloatParams", "FsmInt": "fsmIntParams", "FsmBool": "fsmBoolParams",
    "FsmString": "fsmStringParams", "FsmGameObject": "fsmGameObjectParams", "FsmObject": "fsmObjectParams",
    "FsmVector3": "fsmVector3Params", "FsmVector2": "fsmVector2Params",
}


def named(value: dict, key: str = "value"):
    """Fsm* 变量：用了变量就显示 $名字，否则显示字面值"""
    if value.get("useVariable") and value.get("name"):
        return f"${value['name']}"
    return value.get(key)


def decode(data: dict, kind: str, pos: int, size: int):
    raw = bytes(data["byteData"])
    try:
        if kind in ("Integer", "Enum", "LayerMask"):
            return struct.unpack_from("<i", raw, pos)[0]
        if kind == "Boolean":
            return bool(raw[pos])
        if kind == "Float":
            return round(struct.unpack_from("<f", raw, pos)[0], 3)
        if kind == "FsmEvent":
            return raw[pos:pos + size].decode("utf8", "replace")
        if kind == "String":
            return data["stringParams"][pos]
        if kind in TABLES:
            return named(data[TABLES[kind]][pos])
        if kind == "FsmOwnerDefault":
            owner = data["fsmOwnerDefaultParams"][pos]
            return "owner" if owner["ownerOption"] == 0 else f"go:{named(owner['gameObject'])}"
        if kind == "FsmEnum":
            return named(data["fsmEnumParams"][pos], "intValue")
        if kind == "FsmEventTarget":
            return f"target{data['fsmEventTargetParams'][pos]['target']}"
    except (IndexError, KeyError, struct.error):
        return f"?{kind}"
    return kind


def dump_fsm(fsm: dict, out) -> None:
    out.write(f"=== FSM '{fsm['name']}'\n")
    variables = fsm["variables"]
    for key in ("intVariables", "floatVariables", "boolVariables", "stringVariables"):
        for variable in variables.get(key, []):
            out.write(f"  var {key[:-9]} {variable['name']} = {variable.get('value')}\n")
    for transition in fsm.get("globalTransitions", []):
        out.write(f"  global {transition['fsmEvent']['name']} -> {transition['toState']}\n")
    for state in fsm["states"]:
        out.write(f" [{state['name']}]\n")
        data = state["actionData"]
        starts = list(data["actionStartIndex"]) + [len(data["paramDataType"])]
        for index, action in enumerate(data["actionNames"]):
            params = []
            for j in range(starts[index], starts[index + 1]):
                kind = PARAM_TYPES[data["paramDataType"][j]]
                value = decode(data, kind, data["paramDataPos"][j], data["paramByteDataSize"][j])
                params.append(f"{data['paramName'][j]}={value}")
            enabled = "" if data["actionEnabled"][index] else " [off]"
            out.write(f"    {action.split('.')[-1]}({', '.join(params)}){enabled}\n")
        for transition in state["transitions"]:
            out.write(f"    -> {transition['fsmEvent']['name']} => {transition['toState']}\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("scene")
    parser.add_argument("objects", nargs="+")
    args = parser.parse_args()
    UnityPy.config.FALLBACK_UNITY_VERSION = UNITY_VERSION
    env = UnityPy.load(str(BUNDLE_DIR / f"{args.scene.lower()}.bundle"))
    wanted = set(args.objects)
    for obj in env.objects:
        if obj.type.name != "GameObject":
            continue
        game_object = obj.read()
        if game_object.m_Name not in wanted:
            continue
        sys.stdout.write(f"######## GameObject '{game_object.m_Name}'\n")
        for component in game_object.m_Components:
            target = component.deref()
            if target.type.name != "MonoBehaviour":
                continue
            try:
                tree = target.read_typetree()
            except Exception:  # noqa: BLE001 - 个别组件读不出类型树
                continue
            if isinstance(tree.get("fsm"), dict):
                dump_fsm(tree["fsm"], sys.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main())
