#!/usr/bin/env python3
"""NSO0 解析与解压。无第三方依赖。

NSO 是 Switch 的可执行格式，三个段（.text/.rodata/.data）各自可被 LZ4 块压缩。
本工具提供：
  - Build ID 提取（Atmosphère 金手指 / IPS 补丁的文件名就是它的前 8 字节）
  - 段布局打印（把 Il2CppDumper 的 RVA 换算成运行时地址时必须知道）
  - 解压成扁平镜像（供反汇编器加载）

用法:
    ./tools/nsotool.py info       work/main.nso
    ./tools/nsotool.py buildid    work/main.nso
    ./tools/nsotool.py decompress work/main.nso -o work/main.bin
    ./tools/nsotool.py selftest
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

NSO_MAGIC = b"NSO0"

# NSO0 头部布局（见 switchbrew.org/wiki/NSO）
OFF_FLAGS = 0x0C
OFF_SEG_TEXT = 0x10  # SegmentHeader: file_off, mem_off, decompressed_size
OFF_SEG_RODATA = 0x20
OFF_SEG_DATA = 0x30
OFF_BSS_SIZE = 0x3C
OFF_BUILD_ID = 0x40  # 32 字节，Atmosphère 只用前 8 字节
OFF_CSIZE_TEXT = 0x60
OFF_CSIZE_RODATA = 0x64
OFF_CSIZE_DATA = 0x68

FLAG_TEXT_COMPRESSED = 1 << 0
FLAG_RODATA_COMPRESSED = 1 << 1
FLAG_DATA_COMPRESSED = 1 << 2
# bit 3-5 是各段的 hash 校验开关。
# bit 7 = UseZbicCompression：段用 zstd 而非 LZ4（见 switchbrew.org/wiki/NSO）。
# 我们只实现了 LZ4；遇到这个标志必须明确报错，否则会解出一堆垃圾。
FLAG_ZBIC_COMPRESSION = 1 << 7

LZ4_MIN_MATCH = 4
LZ4_LEN_ESCAPE = 15
BUILD_ID_SIZE = 32
ATMOSPHERE_BUILD_ID_BYTES = 8


class NsoError(ValueError):
    """NSO 解析或解压失败。"""


def lz4_block_decompress(src: bytes, expected_size: int) -> bytearray:
    """LZ4 块格式（非 frame 格式）解压。NSO 段用的就是裸块。

    Args:
        src: 压缩数据。
        expected_size: 期望的解压后长度；传 0 表示不校验。

    Returns:
        解压后的字节。

    Raises:
        NsoError: 数据截断、偏移非法或长度与期望不符。
    """
    out = bytearray()
    pos = 0
    end = len(src)

    while pos < end:
        token = src[pos]
        pos += 1

        # --- 字面量 ---
        literal_len = token >> 4
        if literal_len == LZ4_LEN_ESCAPE:
            while pos < end:
                extra = src[pos]
                pos += 1
                literal_len += extra
                if extra != 0xFF:
                    break
        if literal_len:
            if pos + literal_len > end:
                raise NsoError(f"字面量越界: 需要 {literal_len}，剩余 {end - pos}")
            out += src[pos : pos + literal_len]
            pos += literal_len

        # 最后一个序列只有字面量，没有 match
        if pos >= end:
            break

        # --- 匹配 ---
        if pos + 2 > end:
            raise NsoError("匹配偏移截断")
        offset = src[pos] | (src[pos + 1] << 8)
        pos += 2
        if offset == 0:
            raise NsoError("非法匹配偏移 0")

        match_len = token & 0x0F
        if match_len == LZ4_LEN_ESCAPE:
            while pos < end:
                extra = src[pos]
                pos += 1
                match_len += extra
                if extra != 0xFF:
                    break
        match_len += LZ4_MIN_MATCH

        start = len(out) - offset
        if start < 0:
            raise NsoError(f"匹配偏移超出已解压数据: offset={offset}, pos={len(out)}")
        # 逐字节复制 —— 允许重叠（offset < match_len 是合法且常见的）
        for i in range(match_len):
            out.append(out[start + i])

    if expected_size and len(out) != expected_size:
        raise NsoError(f"解压大小不符: 得到 {len(out)}，期望 {expected_size}")
    return out


class Segment:
    """NSO 的一个段。"""

    def __init__(
        self,
        name: str,
        file_off: int,
        mem_off: int,
        decomp_size: int,
        comp_size: int,
        compressed: bool,
    ) -> None:
        self.name = name
        self.file_off = file_off
        self.mem_off = mem_off
        self.decomp_size = decomp_size
        self.comp_size = comp_size
        self.compressed = compressed

    def __repr__(self) -> str:
        kind = "LZ4" if self.compressed else "raw"
        return (
            f"{self.name:<8} mem_off=0x{self.mem_off:08X} "
            f"size=0x{self.decomp_size:08X} file_off=0x{self.file_off:08X} "
            f"({kind}, 文件内 0x{self.comp_size:X})"
        )


class Nso:
    """解析后的 NSO0 镜像。"""

    def __init__(self, data: bytes) -> None:
        if data[:4] != NSO_MAGIC:
            raise NsoError(
                f"不是 NSO0（magic={data[:4]!r}）。"
                "如果这是 NCA/NSP，先用 nstool 或 hactool 解出 exefs。"
            )
        self.data = data
        self.flags: int = struct.unpack_from("<I", data, OFF_FLAGS)[0]
        if self.flags & FLAG_ZBIC_COMPRESSION:
            raise NsoError(
                "该 NSO 使用 zstd (UseZbicCompression) 压缩，本工具只实现了 LZ4。\n"
                "    用 nstool 先解压（jakcron/nstool 有 macOS arm64 预编译版）"
            )
        self.build_id: bytes = data[OFF_BUILD_ID : OFF_BUILD_ID + BUILD_ID_SIZE]
        self.bss_size: int = struct.unpack_from("<I", data, OFF_BSS_SIZE)[0]

        self.text = self._read_segment(".text", OFF_SEG_TEXT, OFF_CSIZE_TEXT, FLAG_TEXT_COMPRESSED)
        self.rodata = self._read_segment(
            ".rodata", OFF_SEG_RODATA, OFF_CSIZE_RODATA, FLAG_RODATA_COMPRESSED
        )
        self.data_seg = self._read_segment(
            ".data", OFF_SEG_DATA, OFF_CSIZE_DATA, FLAG_DATA_COMPRESSED
        )

    def _read_segment(self, name: str, hdr_off: int, csize_off: int, flag_bit: int) -> Segment:
        file_off, mem_off, decomp_size = struct.unpack_from("<III", self.data, hdr_off)
        comp_size: int = struct.unpack_from("<I", self.data, csize_off)[0]
        return Segment(
            name, file_off, mem_off, decomp_size, comp_size, bool(self.flags & flag_bit)
        )

    @property
    def segments(self) -> list[Segment]:
        return [self.text, self.rodata, self.data_seg]

    def build_id_hex(self, nbytes: int = ATMOSPHERE_BUILD_ID_BYTES) -> str:
        """Atmosphère 用的 Build ID 字符串（前 8 字节，大写十六进制）。"""
        return self.build_id[:nbytes].hex().upper()

    def segment_bytes(self, segment: Segment) -> bytearray:
        """取出某个段的解压后内容。"""
        size = segment.comp_size if segment.compressed else segment.decomp_size
        raw = self.data[segment.file_off : segment.file_off + size]
        if not segment.compressed:
            return bytearray(raw)
        return lz4_block_decompress(raw, segment.decomp_size)

    def flat_image(self) -> bytearray:
        """按 mem_off 铺成扁平镜像，基准为 0。反汇编器加载时用 0 作为 image base。"""
        end = max(s.mem_off + s.decomp_size for s in self.segments)
        image = bytearray(end)
        for segment in self.segments:
            image[segment.mem_off : segment.mem_off + segment.decomp_size] = self.segment_bytes(
                segment
            )
        return image


def _load(path_str: str) -> Nso:
    return Nso(Path(path_str).read_bytes())


def cmd_info(args: argparse.Namespace) -> int:
    nso = _load(args.path)
    print(f"Build ID (前8字节) : {nso.build_id_hex()}")
    print(f"Build ID (完整)     : {nso.build_id.hex().upper()}")
    print(f"Flags               : 0x{nso.flags:X}")
    print(f".bss size           : 0x{nso.bss_size:X}")
    print()
    print("段布局:")
    for segment in nso.segments:
        print(f"  {segment}")
    print()
    print("RVA 换算（待 Il2CppDumper 输出后用实际值复核）:")
    print(f"  .text mem_off = 0x{nso.text.mem_off:X}")
    if nso.text.mem_off == 0:
        print("  mem_off 为 0，RVA 可直接当作模块基址的偏移使用。")
    else:
        print(f"  mem_off 非 0，需确认 RVA 是否已包含它（差值 0x{nso.text.mem_off:X}）。")
    return 0


def cmd_build_id(args: argparse.Namespace) -> int:
    print(_load(args.path).build_id_hex())
    return 0


def cmd_decompress(args: argparse.Namespace) -> int:
    nso = _load(args.path)
    image = nso.flat_image()
    out_path = Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(image)
    print(f"[✓] 写入 {out_path}  ({len(image):,} 字节)")
    for segment in nso.segments:
        print(f"    {segment.name:<8} -> 0x{segment.mem_off:08X}")
    return 0


def _make_header(flags: int) -> bytes:
    """构造一个最小的合法 NSO0 头部，供自检使用。"""
    header = bytearray(0x100)
    header[0:4] = NSO_MAGIC
    struct.pack_into("<I", header, OFF_FLAGS, flags)
    return bytes(header)


def cmd_selftest(_args: argparse.Namespace) -> int:
    """LZ4 块解压器自检。用手工构造的已知向量，不依赖外部库。"""
    failures = 0

    def check(name: str, got: object, want: object) -> None:
        nonlocal failures
        if got != want:
            print(f"  FAIL {name}\n    got : {got!r}\n    want: {want!r}")
            failures += 1
        else:
            print(f"  ok   {name}")

    def expect_error(name: str, fn: object, needle: str = "") -> None:
        nonlocal failures
        try:
            fn()  # type: ignore[operator]
        except NsoError as exc:
            if needle and needle not in str(exc):
                print(f"  FAIL {name}: 报错原因不对 -> {exc}")
                failures += 1
            else:
                print(f"  ok   {name}")
        else:
            print(f"  FAIL {name}: 未触发报错")
            failures += 1

    # 向量 1: 4 字面量 + 匹配 + 结尾字面量
    #   0x44 = 4 字面量, 匹配长度字段 4 (+4 = 8)
    v1 = bytes([0x44]) + b"ABCD" + bytes([0x04, 0x00]) + bytes([0x50]) + b"EFGHI"
    check("字面量 + 匹配 + 结尾字面量", bytes(lz4_block_decompress(v1, 0)), b"ABCDABCDABCDEFGHI")

    # 向量 2: 重叠匹配（offset=1，把单字节铺开）
    #   0x1B = 1 字面量, 匹配长度字段 11 (+4 = 15)
    v2 = bytes([0x1B]) + b"A" + bytes([0x01, 0x00])
    check("offset=1 重叠展开", bytes(lz4_block_decompress(v2, 16)), b"A" * 16)

    # 向量 3: 长字面量（15 + 255 续位编码）
    literals = bytes(range(256)) + bytes(range(14))  # 270 字节
    v3 = bytes([0xF0]) + bytes([0xFF, 0x00]) + literals  # 15 + 255 + 0 = 270
    check("长字面量 255 续位", bytes(lz4_block_decompress(v3, 270)), literals)

    # 向量 4: 长匹配（15 + 续位），offset=4，匹配长度 15+1+4 = 20
    v4 = bytes([0x4F]) + b"ABCD" + bytes([0x04, 0x00]) + bytes([0x01])
    check("长匹配 255 续位", bytes(lz4_block_decompress(v4, 0)), b"ABCD" + b"ABCD" * 5)

    expect_error("大小不符时报错", lambda: lz4_block_decompress(v2, 999), "大小不符")
    expect_error(
        "非法 offset=0 时报错",
        lambda: lz4_block_decompress(bytes([0x00]) + bytes([0x00, 0x00]), 0),
        "非法匹配偏移",
    )
    expect_error("zstd 压缩标志被拒绝", lambda: Nso(_make_header(FLAG_ZBIC_COMPRESSION)), "zstd")
    expect_error("非 NSO0 magic 被拒绝", lambda: Nso(b"ELF\x7f" + bytes(0x100)), "不是 NSO0")

    print()
    if failures:
        print(f"{failures} 项失败")
        return 1
    print("全部通过")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_info = sub.add_parser("info", help="打印 Build ID 与段布局")
    p_info.add_argument("path")
    p_info.set_defaults(func=cmd_info)

    p_build = sub.add_parser("buildid", help="只打印 Build ID 前 8 字节")
    p_build.add_argument("path")
    p_build.set_defaults(func=cmd_build_id)

    p_dec = sub.add_parser("decompress", help="解压成扁平镜像")
    p_dec.add_argument("path")
    p_dec.add_argument("-o", "--output", required=True)
    p_dec.set_defaults(func=cmd_decompress)

    p_test = sub.add_parser("selftest", help="LZ4 解压器自检")
    p_test.set_defaults(func=cmd_selftest)

    args = parser.parse_args()
    try:
        return int(args.func(args))
    except NsoError as exc:
        print(f"[x] {exc}", file=sys.stderr)
        return 1
    except FileNotFoundError as exc:
        print(f"[x] 找不到文件: {exc.filename}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
