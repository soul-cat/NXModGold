#!/usr/bin/env python3
"""NSO0 解析与解压。无第三方依赖。

NSO 是 Switch 的可执行格式，三个段（.text/.rodata/.data）各自可被 LZ4 块压缩。
本工具提供：
  - Build ID 提取（Atmosphère 金手指 / IPS 补丁的文件名就是它的前 8 字节）
  - 段布局打印（把 Il2CppDumper 的 RVA 换算成运行时地址时必须知道）
  - 解压成扁平镜像（供反汇编器加载）

用法:
    ./tools/nsotool.py info      work/exefs/main
    ./tools/nsotool.py buildid   work/exefs/main
    ./tools/nsotool.py decompress work/exefs/main -o work/main.bin
    ./tools/nsotool.py selftest
"""

import argparse
import struct
import sys
from pathlib import Path

NSO_MAGIC = b"NSO0"

# NSO0 头部布局（见 switchbrew.org/wiki/NSO）
OFF_FLAGS = 0x0C
OFF_SEG_TEXT = 0x10      # SegmentHeader: fileOff, memOff, decompressedSize
OFF_SEG_RODATA = 0x20
OFF_SEG_DATA = 0x30
OFF_BSS_SIZE = 0x3C
OFF_BUILD_ID = 0x40      # 32 字节，Atmosphère 只用前 8 字节
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


def lz4BlockDecompress(src: bytes, expectedSize: int) -> bytearray:
  """LZ4 块格式（非 frame 格式）解压。NSO 段用的就是裸块。"""
  out = bytearray()
  i = 0
  n = len(src)

  while i < n:
    token = src[i]
    i += 1

    # --- 字面量 ---
    litLen = token >> 4
    if litLen == 15:
      while i < n:
        b = src[i]
        i += 1
        litLen += b
        if b != 255:
          break
    if litLen:
      if i + litLen > n:
        raise ValueError(f"字面量越界: 需要 {litLen}，剩余 {n - i}")
      out += src[i:i + litLen]
      i += litLen

    # 最后一个序列只有字面量，没有 match
    if i >= n:
      break

    # --- 匹配 ---
    if i + 2 > n:
      raise ValueError("匹配偏移截断")
    offset = src[i] | (src[i + 1] << 8)
    i += 2
    if offset == 0:
      raise ValueError("非法匹配偏移 0")

    matchLen = token & 0x0F
    if matchLen == 15:
      while i < n:
        b = src[i]
        i += 1
        matchLen += b
        if b != 255:
          break
    matchLen += 4  # LZ4 最小匹配长度

    start = len(out) - offset
    if start < 0:
      raise ValueError(f"匹配偏移超出已解压数据: offset={offset}, pos={len(out)}")
    # 逐字节复制 —— 允许重叠（offset < matchLen 是合法且常见的）
    for k in range(matchLen):
      out.append(out[start + k])

  if expectedSize and len(out) != expectedSize:
    raise ValueError(f"解压大小不符: 得到 {len(out)}，期望 {expectedSize}")
  return out


class Segment:
  def __init__(self, name, fileOff, memOff, decompSize, compSize, compressed):
    self.name = name
    self.fileOff = fileOff
    self.memOff = memOff
    self.decompSize = decompSize
    self.compSize = compSize
    self.compressed = compressed

  def __repr__(self):
    c = "LZ4" if self.compressed else "raw"
    return (f"{self.name:<8} memOff=0x{self.memOff:08X} "
            f"size=0x{self.decompSize:08X} fileOff=0x{self.fileOff:08X} "
            f"({c}, 文件内 0x{self.compSize:X})")


class Nso:
  def __init__(self, data: bytes):
    if data[:4] != NSO_MAGIC:
      raise ValueError(f"不是 NSO0（magic={data[:4]!r}）。"
                       "如果这是 NCA/NSP，先用 hactool 解出 exefs。")
    self.data = data
    self.flags = struct.unpack_from("<I", data, OFF_FLAGS)[0]
    if self.flags & FLAG_ZBIC_COMPRESSION:
      raise ValueError(
        "该 NSO 使用 zstd (UseZbicCompression) 压缩，本工具只实现了 LZ4。\n"
        "    用 nstool 先解压：nstool --raw --part0 <out> main\n"
        "    （jakcron/nstool 有 macOS arm64 预编译版）")
    self.buildId = data[OFF_BUILD_ID:OFF_BUILD_ID + 32]
    self.bssSize = struct.unpack_from("<I", data, OFF_BSS_SIZE)[0]

    def seg(name, hdrOff, csizeOff, flagBit):
      fo, mo, ds = struct.unpack_from("<III", data, hdrOff)
      cs = struct.unpack_from("<I", data, csizeOff)[0]
      return Segment(name, fo, mo, ds, cs, bool(self.flags & flagBit))

    self.text = seg(".text", OFF_SEG_TEXT, OFF_CSIZE_TEXT, FLAG_TEXT_COMPRESSED)
    self.rodata = seg(".rodata", OFF_SEG_RODATA, OFF_CSIZE_RODATA, FLAG_RODATA_COMPRESSED)
    self.dataSeg = seg(".data", OFF_SEG_DATA, OFF_CSIZE_DATA, FLAG_DATA_COMPRESSED)

  @property
  def segments(self):
    return [self.text, self.rodata, self.dataSeg]

  def buildIdHex(self, nbytes=8) -> str:
    return self.buildId[:nbytes].hex().upper()

  def segmentBytes(self, s: Segment) -> bytearray:
    raw = self.data[s.fileOff:s.fileOff + (s.compSize if s.compressed else s.decompSize)]
    if not s.compressed:
      return bytearray(raw)
    return lz4BlockDecompress(raw, s.decompSize)

  def flatImage(self) -> bytearray:
    """按 memOff 铺成扁平镜像，基准为 0。反汇编器加载时用 0 作为 image base。"""
    end = max(s.memOff + s.decompSize for s in self.segments)
    img = bytearray(end)
    for s in self.segments:
      img[s.memOff:s.memOff + s.decompSize] = self.segmentBytes(s)
    return img


def cmdInfo(args):
  nso = Nso(Path(args.path).read_bytes())
  print(f"Build ID (前8字节) : {nso.buildIdHex()}")
  print(f"Build ID (完整)     : {nso.buildId.hex().upper()}")
  print(f"Flags               : 0x{nso.flags:X}")
  print(f".bss size           : 0x{nso.bssSize:X}")
  print()
  print("段布局:")
  for s in nso.segments:
    print(f"  {s}")
  print()
  print("RVA 换算：Il2CppDumper 对 NSO 报的 VA 以 .text 的 memOff 为镜像基准。")
  print(f"  运行时地址 = 模块基址 + (VA - 0x{nso.text.memOff:X})")
  print(f"  本文件 .text memOff = 0x{nso.text.memOff:X}"
        f"{'（通常为 0，此时 RVA 可直接用）' if nso.text.memOff == 0 else ''}")
  return 0


def cmdBuildId(args):
  nso = Nso(Path(args.path).read_bytes())
  print(nso.buildIdHex())
  return 0


def cmdDecompress(args):
  nso = Nso(Path(args.path).read_bytes())
  img = nso.flatImage()
  out = Path(args.output)
  out.parent.mkdir(parents=True, exist_ok=True)
  out.write_bytes(img)
  print(f"[✓] 写入 {out}  ({len(img):,} 字节)")
  for s in nso.segments:
    print(f"    {s.name:<8} -> 0x{s.memOff:08X}")
  return 0


def cmdSelftest(args):
  """LZ4 块解压器自检。用手工构造的已知向量，不依赖外部库。"""
  failures = 0

  def check(name, got, want):
    nonlocal failures
    if got != want:
      print(f"  FAIL {name}\n    got : {got!r}\n    want: {want!r}")
      failures += 1
    else:
      print(f"  ok   {name}")

  # 向量 1: 4 字面量 + 重叠匹配 + 结尾字面量
  #   0x44 = 4 字面量, 匹配长度字段 4 (+4 = 8)
  v1 = bytes([0x44]) + b"ABCD" + bytes([0x04, 0x00]) + bytes([0x50]) + b"EFGHI"
  check("字面量 + 匹配 + 结尾字面量",
        bytes(lz4BlockDecompress(v1, 0)), b"ABCDABCDABCDEFGHI")

  # 向量 2: 重叠匹配（offset=1，把单字节铺开）
  #   0x1B = 1 字面量, 匹配长度字段 11 (+4 = 15)
  v2 = bytes([0x1B]) + b"A" + bytes([0x01, 0x00])
  check("offset=1 重叠展开", bytes(lz4BlockDecompress(v2, 16)), b"A" * 16)

  # 向量 3: 长字面量（15 + 255 续位编码）
  lit = bytes(range(256)) + bytes(range(14))  # 270 字节
  body = bytes([0xF0]) + bytes([255, 0]) + lit  # 15 + 255 + 0 = 270
  check("长字面量 255 续位", bytes(lz4BlockDecompress(body, 270)), lit)

  # 向量 4: 长匹配（15 + 续位）
  #   4 字面量 "ABCD"，然后 offset=4，匹配长度 15+1+4 = 20
  v4 = bytes([0x4F]) + b"ABCD" + bytes([0x04, 0x00]) + bytes([0x01])
  check("长匹配 255 续位",
        bytes(lz4BlockDecompress(v4, 0)), b"ABCD" + (b"ABCD" * 5))

  # 向量 5: 大小校验应当报错
  try:
    lz4BlockDecompress(v2, 999)
    print("  FAIL 大小校验未触发")
    failures += 1
  except ValueError:
    print("  ok   大小不符时报错")

  # 向量 6b: zstd 标志应当被拒绝
  hdr = bytearray(0x100)
  hdr[0:4] = b"NSO0"
  struct.pack_into("<I", hdr, OFF_FLAGS, FLAG_ZBIC_COMPRESSION)
  try:
    Nso(bytes(hdr))
    print("  FAIL zstd 标志未被拒绝")
    failures += 1
  except ValueError as e:
    if "zstd" in str(e):
      print("  ok   zstd 压缩标志被拒绝")
    else:
      print(f"  FAIL 报错原因不对: {e}")
      failures += 1

  # 向量 7: 非法 offset 应当报错
  try:
    lz4BlockDecompress(bytes([0x00]) + bytes([0x00, 0x00]), 0)
    print("  FAIL 非法 offset 未触发")
    failures += 1
  except ValueError:
    print("  ok   非法 offset=0 时报错")

  print()
  if failures:
    print(f"{failures} 项失败")
    return 1
  print("全部通过")
  return 0


def main():
  ap = argparse.ArgumentParser(description=__doc__,
                               formatter_class=argparse.RawDescriptionHelpFormatter)
  sub = ap.add_subparsers(dest="cmd", required=True)

  p = sub.add_parser("info", help="打印 Build ID 与段布局")
  p.add_argument("path")
  p.set_defaults(func=cmdInfo)

  p = sub.add_parser("buildid", help="只打印 Build ID 前 8 字节")
  p.add_argument("path")
  p.set_defaults(func=cmdBuildId)

  p = sub.add_parser("decompress", help="解压成扁平镜像")
  p.add_argument("path")
  p.add_argument("-o", "--output", required=True)
  p.set_defaults(func=cmdDecompress)

  p = sub.add_parser("selftest", help="LZ4 解压器自检")
  p.set_defaults(func=cmdSelftest)

  args = ap.parse_args()
  try:
    return args.func(args)
  except (ValueError, FileNotFoundError) as e:
    print(f"[x] {e}", file=sys.stderr)
    return 1


if __name__ == "__main__":
  sys.exit(main())
