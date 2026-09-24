#!/usr/bin/env python3
"""给游戏原版 main.npdm 追加 exlaunch 运行所需的 SVC 权限。

exlaunch 以 subsdk9 形式注入时，要靠 svcMapProcessMemory 给 .text 建 RW 别名
来写 hook，还要用 svcCreateSession/svcReplyAndReceive 拿到本进程句柄。
零售游戏的 npdm 不给这些 SVC，所以必须替换 exefs 里的 main.npdm。

为什么不用 exlaunch 自带的 application.json 生成：那是【别的游戏】的 npdm，
主线程栈大小、system_resource_size、服务列表都未必与 Silksong 一致，
用错了游戏可能起不来。这里只在原版 npdm 上做最小改动：
ACID 与 ACI0 的内核能力（KAC）里补上缺的 SVC 位，其余字节原样保留。

用法:
    ./tools/40-patch-npdm.py patch --in work/dump/ExeFS/main.npdm --out exl/deploy/exefs/main.npdm
    ./tools/40-patch-npdm.py show  work/dump/ExeFS/main.npdm
    ./tools/40-patch-npdm.py selftest       # 需要 devkitPro 的 npdmtool 与 vendor/exlaunch
"""

from __future__ import annotations

import argparse
import json
import shutil
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# exlaunch 运行期实际调用的 SVC（vendor/exlaunch/source/lib 中调用点 grep 所得）。
# svcCallSecureMonitor 只在非 module 模式探测 SoC 时用，这里不需要。
REQUIRED_SVCS: dict[int, str] = {
    0x05: "svcUnmapMemory",
    0x06: "svcQueryMemory",
    0x08: "svcCreateThread",
    0x09: "svcStartThread",
    0x0A: "svcExitThread",
    0x16: "svcCloseHandle",
    0x18: "svcWaitSynchronization",
    0x1E: "svcGetSystemTick",
    0x21: "svcSendSyncRequest",
    0x27: "svcOutputDebugString",
    0x29: "svcGetInfo",
    0x40: "svcCreateSession",
    0x43: "svcReplyAndReceive",
    0x74: "svcMapProcessMemory",
    0x75: "svcUnmapProcessMemory",
}

META_SIZE = 0x80
ACID_SIG_SIZE = 0x100
SECTION_ALIGN = 0x10
SVC_PER_DESCRIPTOR = 0x18
SYSCALL_CAP_PATTERN = 0x0F  # 低 5 位 01111 = EnableSystemCalls
SYSCALL_CAP_MASK = 0x1F

# 容器内三个子段 (offset, size) 表的位置
ACID_TABLE_OFFSET = 0x220
ACI0_TABLE_OFFSET = 0x20
ACID_SIZE_FIELD = 0x204
KAC_INDEX = 2  # 子段顺序：FAC/FAH, SAC, KAC


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


# ---------------------------------------------------------------- 内核能力编解码


def is_syscall_cap(cap: int) -> bool:
    return cap & SYSCALL_CAP_MASK == SYSCALL_CAP_PATTERN


def encode_syscall_cap(index: int, mask: int) -> int:
    return (((index << 24) | mask) << 5) | SYSCALL_CAP_PATTERN


def decode_syscall_cap(cap: int) -> tuple[int, int]:
    """返回 (描述符序号, 24 位掩码)。"""
    return (cap >> 29) & 0x7, (cap >> 5) & 0xFFFFFF


def syscalls_of(caps: list[int]) -> set[int]:
    found: set[int] = set()
    for cap in filter(is_syscall_cap, caps):
        index, mask = decode_syscall_cap(cap)
        found.update(index * SVC_PER_DESCRIPTOR + bit for bit in range(24) if mask >> bit & 1)
    return found


def add_syscalls(caps: list[int], wanted: set[int]) -> list[int]:
    """把 wanted 并入能力表。已有序号的描述符原地置位，没有的按序号插入。

    插入位置与 npdmtool 的输出一致：描述符按序号升序连续排列。
    """
    result = list(caps)
    for svc in sorted(wanted - syscalls_of(caps)):
        index, bit = divmod(svc, SVC_PER_DESCRIPTOR)
        positions = [i for i, cap in enumerate(result) if is_syscall_cap(cap)]
        if not positions:
            raise ValueError("原 npdm 没有任何 SVC 能力描述符，不像是游戏的 npdm")
        same = [i for i in positions if decode_syscall_cap(result[i])[0] == index]
        if same:
            _, mask = decode_syscall_cap(result[same[0]])
            result[same[0]] = encode_syscall_cap(index, mask | 1 << bit)
            continue
        after = [i for i in positions if decode_syscall_cap(result[i])[0] < index]
        insert_at = after[-1] + 1 if after else positions[0]
        result.insert(insert_at, encode_syscall_cap(index, 1 << bit))
    return result


# ---------------------------------------------------------------- 容器（ACID / ACI0）


@dataclass
class Container:
    """ACID 或 ACI0：头部 + 三个子段。offsets 相对容器起点。"""

    blob: bytes
    table_offset: int

    def sections(self) -> list[tuple[int, int]]:
        raw = struct.unpack_from("<6I", self.blob, self.table_offset)
        return [(raw[0], raw[1]), (raw[2], raw[3]), (raw[4], raw[5])]

    def caps(self) -> list[int]:
        offset, size = self.sections()[KAC_INDEX]
        return list(struct.unpack_from(f"<{size // 4}I", self.blob, offset))

    def with_caps(self, caps: list[int]) -> Container:
        """替换 KAC。位于 KAC 之后的子段整体后移（保持 0x10 对齐）。"""
        sections = self.sections()
        kac_offset, kac_size = sections[KAC_INDEX]
        new_kac = struct.pack(f"<{len(caps)}I", *caps)
        tail = self.blob[align_up(kac_offset + kac_size, SECTION_ALIGN) :]
        body = self.blob[:kac_offset] + new_kac
        if tail:
            body += bytes(align_up(len(body), SECTION_ALIGN) - len(body))
        shift = len(body) - (len(self.blob) - len(tail))
        blob = bytearray(body + tail)

        table: list[int] = []
        for index, (offset, size) in enumerate(sections):
            if index == KAC_INDEX:
                size = len(new_kac)
            elif offset > kac_offset:
                offset += shift
            table += [offset, size]
        struct.pack_into("<6I", blob, self.table_offset, *table)
        return Container(bytes(blob), self.table_offset)


# ---------------------------------------------------------------- NPDM


@dataclass
class Npdm:
    meta: bytes
    acid: Container
    aci0: Container
    acid_offset: int
    aci0_offset: int

    @staticmethod
    def parse(data: bytes) -> Npdm:
        if data[:4] != b"META":
            raise ValueError("不是 NPDM（缺 META 魔数）")
        aci0_off, aci0_size, acid_off, acid_size = struct.unpack_from("<4I", data, 0x70)
        acid = data[acid_off : acid_off + acid_size]
        aci0 = data[aci0_off : aci0_off + aci0_size]
        if acid[0x200:0x204] != b"ACID" or aci0[:4] != b"ACI0":
            raise ValueError("ACID/ACI0 魔数不对，npdm 可能已损坏")
        return Npdm(
            data[:META_SIZE],
            Container(acid, ACID_TABLE_OFFSET),
            Container(aci0, ACI0_TABLE_OFFSET),
            acid_off,
            aci0_off,
        )

    def title_id(self) -> int:
        return struct.unpack_from("<Q", self.aci0.blob, 0x10)[0]

    def serialize(self) -> bytes:
        """两个容器按原先的先后顺序排布：尽量留在原位，放不下就顺延并 0x10 对齐。"""
        acid = bytearray(self.acid.blob)
        struct.pack_into("<I", acid, ACID_SIZE_FIELD, len(acid) - ACID_SIG_SIZE)
        parts = sorted(
            [(self.acid_offset, "acid", bytes(acid)), (self.aci0_offset, "aci0", self.aci0.blob)]
        )
        out = bytearray(self.meta)
        placed: dict[str, tuple[int, int]] = {}
        for original_offset, name, blob in parts:
            start = max(original_offset, align_up(len(out), SECTION_ALIGN))
            out += bytes(start - len(out)) + blob
            placed[name] = (start, len(blob))
        struct.pack_into("<4I", out, 0x70, *placed["aci0"], *placed["acid"])
        return bytes(out)


def patch_npdm(data: bytes, wanted: set[int]) -> tuple[bytes, set[int]]:
    """返回 (新 npdm, 实际新增的 SVC)。ACID 与 ACI0 同步补齐（loader 要求 ACI0 ⊆ ACID）。"""
    npdm = Npdm.parse(data)
    missing = (wanted - syscalls_of(npdm.aci0.caps())) | (wanted - syscalls_of(npdm.acid.caps()))
    if not missing:
        return data, set()
    npdm.acid = npdm.acid.with_caps(add_syscalls(npdm.acid.caps(), wanted))
    npdm.aci0 = npdm.aci0.with_caps(add_syscalls(npdm.aci0.caps(), wanted))
    return npdm.serialize(), missing


# ---------------------------------------------------------------- 命令


def svc_name(svc: int) -> str:
    return REQUIRED_SVCS.get(svc, f"svc#{svc:#04x}")


def cmd_patch(args: argparse.Namespace) -> int:
    src = Path(args.input)
    if not src.is_file():
        print(f"[x] 找不到 {src}", file=sys.stderr)
        print("    它来自游戏 exefs：./tools/05-unpack-container.sh 会放到 work/dump/ExeFS/", file=sys.stderr)
        return 1
    data = src.read_bytes()
    patched, added = patch_npdm(data, set(REQUIRED_SVCS))
    title_id = Npdm.parse(patched).title_id()
    if args.title_id and title_id != int(args.title_id, 16):
        print(f"[x] npdm 的 title id {title_id:016X} 不是 {args.title_id}", file=sys.stderr)
        return 2

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(patched)
    print(f"[✓] {out}（title {title_id:016X}，{len(data)} → {len(patched)} 字节）")
    for svc in sorted(added):
        print(f"    + {svc:#04x} {svc_name(svc)}")
    return 0


def cmd_show(args: argparse.Namespace) -> int:
    npdm = Npdm.parse(Path(args.path).read_bytes())
    print(f"title id : {npdm.title_id():016X}")
    for name, container in (("ACID", npdm.acid), ("ACI0", npdm.aci0)):
        svcs = syscalls_of(container.caps())
        lacking = sorted(set(REQUIRED_SVCS) - svcs)
        print(f"{name}     : {len(svcs)} 个 SVC；exlaunch 缺 {[svc_name(s) for s in lacking] or '无'}")
    return 0


# ---------------------------------------------------------------- 自检


class SelftestError(AssertionError):
    """自检失败。不用 assert：python -O 会把它整个删掉。"""


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise SelftestError(message)


def find_npdmtool() -> str:
    found = shutil.which("npdmtool") or "/opt/devkitpro/tools/bin/npdmtool"
    if not Path(found).is_file():
        raise FileNotFoundError("找不到 npdmtool（dkp-pacman -S switch-dev 会装上）")
    return found


def build_with_npdmtool(spec: dict, workdir: Path, name: str) -> bytes:
    json_path, npdm_path = workdir / f"{name}.json", workdir / f"{name}.npdm"
    json_path.write_text(json.dumps(spec), encoding="utf-8")
    subprocess.run([find_npdmtool(), str(json_path), str(npdm_path)], check=True, capture_output=True)
    return npdm_path.read_bytes()


def check_against_npdmtool(template: dict, workdir: Path) -> None:
    """补丁结果必须与 npdmtool 直接生成的"带全部 SVC 的 npdm"逐字节一致。"""
    base = build_with_npdmtool(template, workdir, "base")
    expected_spec = json.loads(json.dumps(template))
    syscalls = next(c for c in expected_spec["kernel_capabilities"] if c["type"] == "syscalls")
    syscalls["value"].update({name: f"{svc:#04x}" for svc, name in REQUIRED_SVCS.items()})
    expected = build_with_npdmtool(expected_spec, workdir, "expected")

    patched, added = patch_npdm(base, set(REQUIRED_SVCS))
    expect(added, "模板本应缺少 exlaunch 所需 SVC")
    expect(patched == expected, "补丁结果与 npdmtool 输出不一致")
    again, added_again = patch_npdm(patched, set(REQUIRED_SVCS))
    expect(again == patched and not added_again, "补丁不是幂等的")
    print(f"  ✓ npdmtool 对照：补 {len(added)} 个 SVC，{len(base)} → {len(patched)} 字节，逐字节一致；幂等")


def check_section_shift() -> None:
    """KAC 不在末尾时，其后的子段整体后移且内容不变。"""
    sac = b"\x04hid\x00\x00"
    head = bytearray(0x40)
    struct.pack_into("<6I", head, ACI0_TABLE_OFFSET, 0x40, 0x1C, 0x70, len(sac), 0x60, 8)
    blob = bytes(head) + bytes(0x20) + struct.pack("<2I", encode_syscall_cap(0, 1), 0x7) + bytes(8) + sac
    grown = Container(blob, ACI0_TABLE_OFFSET).with_caps(
        [encode_syscall_cap(0, 1), 0x7, encode_syscall_cap(2, 1), encode_syscall_cap(4, 1), 0x3FFF]
    )
    (_, _), (sac_off, sac_size), (kac_off, kac_size) = grown.sections()
    expect((kac_off, kac_size) == (0x60, 20) and sac_off == 0x80, f"子段表不对: {grown.sections()}")
    expect(grown.blob[sac_off : sac_off + sac_size] == sac, "后移后的 SAC 内容被破坏")
    expect(len(grown.blob) == 0x80 + len(sac), "容器总长不对")
    print("  ✓ KAC 之后的子段：后移 0x10 且内容完整")


def check_container_order(template: dict, workdir: Path) -> None:
    """ACI0 在 ACID 之前的布局（官方 npdm 可能如此）也能正确重排。"""
    npdm = Npdm.parse(build_with_npdmtool(template, workdir, "order"))
    npdm.aci0_offset, npdm.acid_offset = npdm.acid_offset, npdm.acid_offset + 0x1000
    swapped = Npdm.parse(npdm.serialize())
    expect(swapped.aci0_offset < swapped.acid_offset, "ACI0 应排在 ACID 之前")
    expect(swapped.aci0.blob == npdm.aci0.blob and swapped.acid.caps() == npdm.acid.caps(), "重排后容器内容变了")
    patched = Npdm.parse(patch_npdm(npdm.serialize(), set(REQUIRED_SVCS))[0])
    expect(set(REQUIRED_SVCS) <= syscalls_of(patched.aci0.caps()), "ACI0 未补齐 SVC")
    expect(set(REQUIRED_SVCS) <= syscalls_of(patched.acid.caps()), "ACID 未补齐 SVC")
    print("  ✓ ACI0 在前的布局：重排与补丁均正确")


def cmd_selftest(_: argparse.Namespace) -> int:
    template_path = REPO_ROOT / "vendor" / "exlaunch" / "misc" / "npdm-json" / "application.json"
    if not template_path.is_file():
        print(f"[x] 找不到 {template_path}（先按 HANDOFF 克隆 vendor/exlaunch）", file=sys.stderr)
        return 1
    template = json.loads(template_path.read_text(encoding="utf-8"))
    print("40-patch-npdm 自检")
    with tempfile.TemporaryDirectory() as tmp:
        check_against_npdmtool(template, Path(tmp))
        check_section_shift()
        check_container_order(template, Path(tmp))
    print("全部通过")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = parser.add_subparsers(dest="command", required=True)
    patch = sub.add_parser("patch", help="生成补好 SVC 的 main.npdm")
    patch.add_argument("--in", dest="input", default=str(REPO_ROOT / "work" / "dump" / "ExeFS" / "main.npdm"))
    patch.add_argument("--out", required=True)
    patch.add_argument("--title-id", default="010013C00E930000", help="校验 npdm 属于这个游戏；传空跳过")
    patch.set_defaults(func=cmd_patch)
    show = sub.add_parser("show", help="查看 npdm 的 title id 与 SVC 缺口")
    show.add_argument("path")
    show.set_defaults(func=cmd_show)
    sub.add_parser("selftest", help="与 npdmtool 对照验证").set_defaults(func=cmd_selftest)
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
