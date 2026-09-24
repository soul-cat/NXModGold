"""30-gen-offsets.py 的补充：dump.cs 里没有、要从 script.json / main.bin 取的地址。

三类（tools/targets.json 的 "script" 段）：

- method        : ScriptMethod 按完整名字取地址。共享泛型实例（如
                  `SceneData.PersistentItemDataCollection<bool, object>$$TryGetValue`）只在这里有。
- metadata_slot : ScriptMetadataMethod 的槽位地址（`Method$...`）。调用共享泛型实现时，
                  必须把槽里的 MethodInfo* 作为最后一个参数传进去。
- first_bl_in   : 某个方法里第一条 BL 的目标。IL2CPP 的
                  `il2cpp_codegen_initialize_runtime_metadata` 没有符号，但每个用到
                  metadata 槽的方法开头都先调它，取第一条 BL 即可。
"""

from __future__ import annotations

import json
import struct
from pathlib import Path
from typing import Any

BL_MASK = 0xFC000000
BL_OPCODE = 0x94000000
BL_SCAN_LIMIT = 64  # 只看方法开头 64 条指令


class ScriptIndex:
    """script.json 的按名索引。"""

    def __init__(self, path: Path) -> None:
        data = json.loads(path.read_text(encoding="utf-8"))
        self.methods = {entry["Name"]: entry["Address"] for entry in data["ScriptMethod"]}
        self.slots = {entry["Name"]: entry["Address"] for entry in data["ScriptMetadataMethod"]}


def first_bl_target(image: bytes, rva: int) -> int | None:
    """从 rva 开始找第一条 BL，返回其目标地址。"""
    for index in range(BL_SCAN_LIMIT):
        pc = rva + index * 4
        word = struct.unpack_from("<I", image, pc)[0]
        if word & BL_MASK == BL_OPCODE:
            offset = word & 0x03FFFFFF
            if offset & 0x02000000:
                offset -= 0x04000000
            return pc + offset * 4
    return None


def _resolve(spec: dict[str, Any], index: ScriptIndex, image: bytes | None) -> int | None:
    kind, name = spec["kind"], spec["script_name"]
    if kind == "method":
        return index.methods.get(name)
    if kind == "metadata_slot":
        return index.slots.get(name)
    if kind == "first_bl_in":
        rva = index.methods.get(name)
        if rva is None or image is None:
            return None
        return first_bl_target(image, rva)
    raise ValueError(f"未知的 script 条目类型: {kind}")


def emit_script_entries(
    specs: list[dict[str, Any]], script_path: Path, main_bin: Path, missing: list[str]
) -> list[str]:
    """返回要写进 offsets.h 的行；找不到的必需条目追加到 missing。"""
    if not specs:
        return []
    if not script_path.is_file():
        missing.append(f"script.json（{script_path} 不存在，先跑 20-dump-il2cpp.sh）")
        return []
    index = ScriptIndex(script_path)
    image = main_bin.read_bytes() if main_bin.is_file() else None
    lines = ["", "// ---------------------------------------------------------------- script.json / 反汇编"]
    for spec in specs:
        address = _resolve(spec, index, image)
        if address is None or address == 0:
            if spec.get("required", True):
                missing.append(f"{spec['name']}（{spec['kind']}: {spec['script_name']}）")
            continue
        lines.append(f"#define {spec['name']:<22} 0x{address:X}   // {spec['kind']}: {spec['script_name']}")
    return lines
