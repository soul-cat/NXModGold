#!/usr/bin/env python3
"""从 Il2CppDumper 产出的 dump.cs 提取字段偏移与方法 RVA，生成 plugin/source/offsets.h。

dump.cs 的格式（Il2CppDumper 输出）：

    // Namespace:
    public class HealthManager : MonoBehaviour
    {
        // Fields
        public int hp; // 0x20
        private bool isDead; // 0x24

        // Methods

        // RVA: 0x1A2B3C4 Offset: 0x1A2B3C4 VA: 0x71A2B3C4
        public void TakeDamage(HitInstance hitInstance) { }
    }

用法:
    ./tools/30-gen-offsets.py
    ./tools/30-gen-offsets.py --dump work/dump.cs --out plugin/source/offsets.h
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parent.parent

RE_NAMESPACE = re.compile(r"^//\s*Namespace:\s*(\S*)\s*$")
RE_TYPE = re.compile(
    r"^\s*(?:\[[^\]]*\]\s*)*"
    r"(?:public|private|internal|protected)?\s*"
    r"(?:static\s+|sealed\s+|abstract\s+|readonly\s+)*"
    r"(?:class|struct|interface|enum)\s+"
    r"([A-Za-z_][A-Za-z0-9_]*)"
)
RE_FIELD = re.compile(r"^\s*.*?\b([A-Za-z_][A-Za-z0-9_]*)\s*;\s*//\s*(0x[0-9A-Fa-f]+)\s*$")
RE_RVA = re.compile(
    r"^\s*//\s*RVA:\s*(0x[0-9A-Fa-f]+|-1)\s+Offset:\s*(0x[0-9A-Fa-f]+|-1)"
    r"(?:\s+VA:\s*(0x[0-9A-Fa-f]+))?"
)
RE_METHOD = re.compile(r"\b([A-Za-z_][A-Za-z0-9_<>]*)\s*\(")


@dataclass
class MethodEntry:
    """dump.cs 里的一个方法。rva 为 None 表示 Il2CppDumper 报了 -1（无代码）。"""

    rva: int | None
    va: int | None
    signature: str


@dataclass
class TypeInfo:
    """dump.cs 里的一个类型。"""

    namespace: str
    name: str
    fields: dict[str, int] = field(default_factory=dict)
    methods: dict[str, list[MethodEntry]] = field(default_factory=dict)


def parse_dump(path: Path) -> dict[tuple[str, str], TypeInfo]:
    """把 dump.cs 解析成 {(namespace, typename): TypeInfo}。"""
    types: dict[tuple[str, str], TypeInfo] = {}
    namespace = ""
    current: TypeInfo | None = None
    pending_rva: tuple[int | None, int | None] | None = None
    depth = 0

    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for raw_line in handle:
            line = raw_line.rstrip("\n")

            ns_match = RE_NAMESPACE.match(line)
            if ns_match is not None:
                namespace = ns_match.group(1)
                continue

            rva_match = RE_RVA.match(line)
            if rva_match is not None:
                rva = None if rva_match.group(1) == "-1" else int(rva_match.group(1), 16)
                va = int(rva_match.group(3), 16) if rva_match.group(3) else None
                pending_rva = (rva, va)
                continue

            # 类型声明：排除方法体里带花括号的行
            if depth == 0:
                type_match = RE_TYPE.match(line)
                if type_match is not None and "(" not in line.split("//")[0]:
                    current = TypeInfo(namespace=namespace, name=type_match.group(1))
                    types[(namespace, current.name)] = current
                    pending_rva = None
                    depth += line.count("{") - line.count("}")
                    continue

            if current is None:
                continue

            depth += line.count("{") - line.count("}")
            if depth <= 0:
                if line.strip().startswith("}") and depth == 0:
                    current = None
                    pending_rva = None
                continue

            field_match = RE_FIELD.match(line)
            if field_match is not None and pending_rva is None:
                current.fields[field_match.group(1)] = int(field_match.group(2), 16)
                continue

            if pending_rva is not None:
                method_match = RE_METHOD.search(line.split("//")[0])
                if method_match is not None:
                    name = method_match.group(1)
                    current.methods.setdefault(name, []).append(
                        MethodEntry(rva=pending_rva[0], va=pending_rva[1], signature=line.strip())
                    )
                pending_rva = None

    return types


def find_type(
    types: dict[tuple[str, str], TypeInfo], name: str, namespace: str | None
) -> TypeInfo | None:
    """按类型名查找；未指定命名空间时允许唯一匹配，否则优先全局命名空间。"""
    if namespace is not None:
        return types.get((namespace, name))

    hits = [info for (_, type_name), info in types.items() if type_name == name]
    if len(hits) == 1:
        return hits[0]
    if len(hits) > 1:
        # 游戏自身代码通常在全局命名空间
        global_hits = [info for info in hits if info.namespace == ""]
        if len(global_hits) == 1:
            return global_hits[0]
    return None


def _emit_fields(
    specs: list[dict[str, Any]],
    types: dict[tuple[str, str], TypeInfo],
    lines: list[str],
    missing: list[str],
) -> None:
    for spec in specs:
        info = find_type(types, spec["class"], spec.get("namespace"))
        offset: int | None = None
        matched_name: str | None = None
        if info is not None:
            for candidate in [spec["field"], *spec.get("aliases", [])]:
                if candidate in info.fields:
                    offset, matched_name = info.fields[candidate], candidate
                    break

        if offset is None:
            if spec.get("required"):
                missing.append(spec["name"])
            else:
                lines.append(f"// [缺失] {spec['name']}: {spec['class']}.{spec['field']} 未找到")
            continue

        lines.append(f"#define {spec['name']:<22} 0x{offset:X}   // {spec['class']}.{matched_name}")


def _emit_methods(
    specs: list[dict[str, Any]],
    types: dict[tuple[str, str], TypeInfo],
    text_base: int,
    lines: list[str],
    missing: list[str],
) -> None:
    for spec in specs:
        info = find_type(types, spec["class"], spec.get("namespace"))
        entries = info.methods.get(spec["method"], []) if info is not None else []
        usable = [entry for entry in entries if entry.rva is not None]

        if not usable:
            note = "RVA 为 -1（无代码，可能被裁剪）" if entries else "未找到"
            if spec.get("required"):
                missing.append(f"{spec['name']}（{note}）")
            else:
                lines.append(f"// [缺失] {spec['name']}: {spec['class']}.{spec['method']} {note}")
            continue

        if len(usable) > 1:
            lines.append(
                f"// 注意：{spec['class']}.{spec['method']} 有 {len(usable)} 个重载，取第一个"
            )
            for entry in usable:
                lines.append(f"//   RVA 0x{entry.rva:X}  {entry.signature[:90]}")

        chosen = usable[0]
        assert chosen.rva is not None  # usable 已过滤
        if chosen.va is not None and text_base:
            module_offset = chosen.va - text_base
        else:
            module_offset = chosen.rva
        lines.append(
            f"#define {spec['name']:<22} 0x{module_offset:X}   "
            f"// {spec['class']}.{spec['method']}"
        )


def build_header(
    types: dict[tuple[str, str], TypeInfo],
    targets: dict[str, Any],
    build_id: str,
    text_base: int,
) -> tuple[list[str], list[str]]:
    """生成 offsets.h 的行，以及缺失的必需符号列表。"""
    lines = [
        "// 由 tools/30-gen-offsets.py 自动生成 —— 不要手改。",
        "// 游戏每次更新后重新运行该脚本。",
        "//",
        f"// Build ID  : {build_id or '(未提供)'}",
        f"// .text base: {hex(text_base)}",
        "",
        "#ifndef SS_OFFSETS_H",
        "#define SS_OFFSETS_H",
        "",
        "#include <stdint.h>",
        "",
        f'#define SS_BUILD_ID_STR "{build_id}"',
        "",
        "// ---------------------------------------------------------------- 字段偏移",
    ]
    missing: list[str] = []

    _emit_fields(targets.get("fields", []), types, lines, missing)
    lines += ["", "// ---------------------------------------------------------------- 方法 RVA"]
    _emit_methods(targets.get("methods", []), types, text_base, lines, missing)
    lines += ["", "#endif  // SS_OFFSETS_H", ""]

    return lines, missing


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--dump", default=str(REPO_ROOT / "work" / "dump.cs"), help="dump.cs 路径")
    parser.add_argument("--targets", default=str(REPO_ROOT / "tools" / "targets.json"))
    parser.add_argument("--out", default=str(REPO_ROOT / "plugin" / "source" / "offsets.h"))
    parser.add_argument("--build-id", default="", help="游戏 Build ID（16 位十六进制）")
    parser.add_argument(
        "--text-base",
        default="0x0",
        help="NSO .text 段的虚拟地址基准，用于把 VA 换算成模块内偏移",
    )
    args = parser.parse_args()

    dump_path = Path(args.dump)
    if not dump_path.is_file():
        print(f"[x] 找不到 {dump_path}", file=sys.stderr)
        print("    先跑 ./tools/20-dump-il2cpp.sh", file=sys.stderr)
        return 1

    print(f"[*] 解析 {dump_path} …")
    types = parse_dump(dump_path)
    print(f"    共 {len(types)} 个类型")

    targets = json.loads(Path(args.targets).read_text(encoding="utf-8"))
    text_base = int(args.text_base, 16)

    lines, missing = build_header(types, targets, args.build_id, text_base)

    if missing:
        print("[x] 必需符号缺失：" + ", ".join(missing), file=sys.stderr)
        print("    检查 tools/targets.json 里的类名/字段名是否与本版本一致：", file=sys.stderr)
        print(f"      grep -n 'class HealthManager' {dump_path}", file=sys.stderr)
        return 2

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(lines), encoding="utf-8")
    define_count = sum(1 for line in lines if line.startswith("#define"))
    print(f"[✓] 已写入 {out_path}")
    print(f"    {define_count} 个符号")
    return 0


if __name__ == "__main__":
    sys.exit(main())
