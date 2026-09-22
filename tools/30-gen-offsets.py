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
    ./tools/30-gen-offsets.py                       # 用默认路径
    ./tools/30-gen-offsets.py --dump work/dump.cs --out plugin/source/offsets.h
"""

import argparse
import json
import re
import sys
from dataclasses import dataclass, field as dc_field
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

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
class TypeInfo:
  namespace: str
  name: str
  fields: dict = dc_field(default_factory=dict)   # field name -> int offset
  methods: dict = dc_field(default_factory=dict)  # method name -> list[(rva, va, sig)]


def parseDump(path: Path) -> dict:
  """把 dump.cs 解析成 {(namespace, typename): TypeInfo}。"""
  types = {}
  namespace = ""
  current = None
  pendingRva = None
  depth = 0

  with path.open("r", encoding="utf-8", errors="replace") as fh:
    for raw in fh:
      line = raw.rstrip("\n")

      m = RE_NAMESPACE.match(line)
      if m:
        namespace = m.group(1)
        continue

      m = RE_RVA.match(line)
      if m:
        rva = None if m.group(1) == "-1" else int(m.group(1), 16)
        va = int(m.group(3), 16) if m.group(3) else None
        pendingRva = (rva, va)
        continue

      # 类型声明：注意要排除方法体里的花括号
      if depth == 0:
        m = RE_TYPE.match(line)
        if m and "(" not in line.split("//")[0]:
          current = TypeInfo(namespace=namespace, name=m.group(1))
          types[(namespace, current.name)] = current
          pendingRva = None
          depth += line.count("{") - line.count("}")
          continue

      if current is None:
        continue

      depth += line.count("{") - line.count("}")
      if depth <= 0:
        # 类型结束
        if line.strip().startswith("}") and depth == 0:
          current = None
          pendingRva = None
        continue

      m = RE_FIELD.match(line)
      if m and pendingRva is None:
        current.fields[m.group(1)] = int(m.group(2), 16)
        continue

      if pendingRva is not None:
        m = RE_METHOD.search(line.split("//")[0])
        if m:
          name = m.group(1)
          current.methods.setdefault(name, []).append(
            (pendingRva[0], pendingRva[1], line.strip())
          )
        pendingRva = None

  return types


def findType(types: dict, name: str, namespace: str | None):
  """按类型名查找；未指定命名空间时允许唯一匹配。"""
  if namespace is not None:
    return types.get((namespace, name))
  hits = [t for (ns, n), t in types.items() if n == name]
  if len(hits) == 1:
    return hits[0]
  if len(hits) > 1:
    # 优先全局命名空间（游戏自身代码通常在全局）
    globals_ = [t for t in hits if t.namespace == ""]
    if len(globals_) == 1:
      return globals_[0]
  return None


def main() -> int:
  ap = argparse.ArgumentParser(description=__doc__,
                               formatter_class=argparse.RawDescriptionHelpFormatter)
  ap.add_argument("--dump", default=str(REPO / "work" / "dump.cs"), help="dump.cs 路径")
  ap.add_argument("--targets", default=str(REPO / "tools" / "targets.json"))
  ap.add_argument("--out", default=str(REPO / "plugin" / "source" / "offsets.h"))
  ap.add_argument("--build-id", default="", help="游戏 Build ID（16 位十六进制）")
  ap.add_argument("--text-base", default="0x0",
                  help="NSO .text 段的虚拟地址基准，用于把 VA 换算成模块内偏移")
  args = ap.parse_args()

  dumpPath = Path(args.dump)
  if not dumpPath.is_file():
    print(f"[x] 找不到 {dumpPath}", file=sys.stderr)
    print("    先跑 ./tools/20-dump-il2cpp.sh", file=sys.stderr)
    return 1

  print(f"[*] 解析 {dumpPath} …")
  types = parseDump(dumpPath)
  print(f"    共 {len(types)} 个类型")

  targets = json.loads(Path(args.targets).read_text(encoding="utf-8"))
  textBase = int(args.text_base, 16)

  lines = [
    "// 由 tools/30-gen-offsets.py 自动生成 —— 不要手改。",
    "// 游戏每次更新后重新运行该脚本。",
    "//",
    f"// Build ID: {args.build_id or '(未提供)'}",
    f"// .text base: {hex(textBase)}",
    "",
    "#ifndef SS_OFFSETS_H",
    "#define SS_OFFSETS_H",
    "",
    "#include <stdint.h>",
    "",
    f'#define SS_BUILD_ID_STR "{args.build_id}"',
    "",
    "// ---------------------------------------------------------------- 字段偏移",
  ]

  missing = []

  for spec in targets.get("fields", []):
    ti = findType(types, spec["class"], spec.get("namespace"))
    val = None
    used = None
    if ti:
      for cand in [spec["field"]] + spec.get("aliases", []):
        if cand in ti.fields:
          val, used = ti.fields[cand], cand
          break
    if val is None:
      (missing if spec.get("required") else lines).append(
        spec["name"] if spec.get("required")
        else f"// [缺失] {spec['name']}: {spec['class']}.{spec['field']} 未找到")
      if not spec.get("required"):
        continue
    else:
      lines.append(f"#define {spec['name']:<22} 0x{val:X}   // {spec['class']}.{used}")

  lines += ["", "// ---------------------------------------------------------------- 方法 RVA"]

  for spec in targets.get("methods", []):
    ti = findType(types, spec["class"], spec.get("namespace"))
    entries = ti.methods.get(spec["method"], []) if ti else []
    entries = [e for e in entries if e[0] is not None]
    if not entries:
      if spec.get("required"):
        missing.append(spec["name"])
      else:
        lines.append(f"// [缺失] {spec['name']}: {spec['class']}.{spec['method']} 未找到")
      continue
    if len(entries) > 1:
      lines.append(f"// 注意：{spec['class']}.{spec['method']} 有 {len(entries)} 个重载，取第一个")
      for rva, va, sig in entries:
        lines.append(f"//   RVA 0x{rva:X}  {sig[:90]}")
    rva, va, sig = entries[0]
    off = (va - textBase) if (va is not None and textBase) else rva
    lines.append(f"#define {spec['name']:<22} 0x{off:X}   // {spec['class']}.{spec['method']}")

  lines += ["", "#endif  // SS_OFFSETS_H", ""]

  if missing:
    print("[x] 必需符号缺失：" + ", ".join(missing), file=sys.stderr)
    print("    检查 tools/targets.json 里的类名/字段名是否与本版本一致。", file=sys.stderr)
    print("    可用以下命令自查：", file=sys.stderr)
    print(f"      grep -n 'class HealthManager' {dumpPath}", file=sys.stderr)
    return 2

  outPath = Path(args.out)
  outPath.parent.mkdir(parents=True, exist_ok=True)
  outPath.write_text("\n".join(lines), encoding="utf-8")
  print(f"[✓] 已写入 {outPath}")
  print(f"    {sum(1 for l in lines if l.startswith('#define'))} 个符号")
  return 0


if __name__ == "__main__":
  sys.exit(main())
