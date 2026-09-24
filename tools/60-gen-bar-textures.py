#!/usr/bin/env python3
"""把 BOSS 血条贴图（exl/assets/*.png，作者自绘）切成三段，生成 exl/program/bar_textures.gen.h。

贴图是纯白 + alpha，运行时按颜色染色。IMGUI 在本游戏里没有按纹理坐标画的接口（DrawTextureWithTexCoords
被裁掉了），所以离线切成 左端 / 中段 / 右端 三张小贴图：两端按原比例画，中段横向拉伸（三宫格），
尖角在任何宽度下都不变形。端头宽度自动识别：从两边往里找第一列与正中间那一列完全相同的位置。

Unity 的原始纹理数据是自下而上存行，这里提前上下翻转。

用法:
    ./tools/60-gen-bar-textures.py            # 生成
    ./tools/60-gen-bar-textures.py selftest
"""

from __future__ import annotations

import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
ASSETS = REPO_ROOT / "exl" / "assets"
OUT = REPO_ROOT / "exl" / "program" / "bar_textures.gen.h"
MIDDLE_WIDTH = 4  # 中段只需几列，反正要拉伸
TEXTURES = (("BOSS_FRAME", "boss_frame.png"), ("BOSS_FILL", "boss_fill.png"))
BYTES_PER_LINE = 24
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
RGBA_COLOR_TYPE = 6
BYTES_PER_PIXEL = 4


@dataclass
class Image:
    """RGBA8 位图，rows[y] 为自上而下第 y 行的字节串。"""

    width: int
    height: int
    rows: list[bytes]

    def column(self, x: int) -> list[bytes]:
        return [row[x * BYTES_PER_PIXEL:(x + 1) * BYTES_PER_PIXEL] for row in self.rows]

    def crop(self, left: int, right: int) -> Image:
        rows = [row[left * BYTES_PER_PIXEL:right * BYTES_PER_PIXEL] for row in self.rows]
        return Image(right - left, self.height, rows)


def paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def unfilter(kind: int, line: bytearray, previous: bytes) -> bytes:
    for i in range(len(line)):
        left = line[i - BYTES_PER_PIXEL] if i >= BYTES_PER_PIXEL else 0
        up = previous[i]
        corner = previous[i - BYTES_PER_PIXEL] if i >= BYTES_PER_PIXEL else 0
        predictor = (0, left, up, (left + up) // 2, paeth(left, up, corner))[kind]
        line[i] = (line[i] + predictor) & 0xFF
    return bytes(line)


def read_png(path: Path) -> Image:
    """只支持本项目贴图用到的格式：8 位 RGBA、非隔行。"""
    data = path.read_bytes()
    if data[:8] != PNG_SIGNATURE:
        raise ValueError(f"{path} 不是 PNG")
    offset, chunks, header = 8, [], None
    while offset < len(data):
        length, kind = struct.unpack(">I4s", data[offset:offset + 8])
        body = data[offset + 8:offset + 8 + length]
        header = body if kind == b"IHDR" else header
        chunks += [body] if kind == b"IDAT" else []
        offset += 12 + length
    width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", header)
    if (depth, color, interlace) != (8, RGBA_COLOR_TYPE, 0):
        raise ValueError(f"{path}: 只支持 8 位 RGBA 非隔行，实际 depth={depth} color={color}")
    raw, stride, rows = zlib.decompress(b"".join(chunks)), width * BYTES_PER_PIXEL, []
    previous = bytes(stride)
    for y in range(height):
        start = y * (stride + 1)
        previous = unfilter(raw[start], bytearray(raw[start + 1:start + 1 + stride]), previous)
        rows.append(previous)
    return Image(width, height, rows)


def cap_widths(image: Image) -> tuple[int, int]:
    """(左端宽, 右端宽)：两边往里第一列与中间列相同的位置。"""
    middle = image.column(image.width // 2)
    left = next(x for x in range(image.width) if image.column(x) == middle)
    right = next(x for x in range(image.width) if image.column(image.width - 1 - x) == middle)
    return left, right


def slice_image(image: Image) -> dict[str, Image]:
    left, right = cap_widths(image)
    mid_start = image.width // 2 - MIDDLE_WIDTH // 2
    return {
        "LEFT": image.crop(0, max(left, 1)),
        "MID": image.crop(mid_start, mid_start + MIDDLE_WIDTH),
        "RIGHT": image.crop(image.width - max(right, 1), image.width),
    }


def raw_rgba_bottom_up(image: Image) -> bytes:
    return b"".join(reversed(image.rows))


def render_blob(name: str, image: Image.Image) -> list[str]:
    data = raw_rgba_bottom_up(image)
    lines = [f"inline constexpr uint8_t {name}_PIXELS[] = {{"]
    for i in range(0, len(data), BYTES_PER_LINE):
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + BYTES_PER_LINE]) + ",")
    lines.append("};")
    lines.append(f"inline constexpr TextureBlob {name}{{{image.width}, {image.height}, {name}_PIXELS}};")
    return lines


def render_header() -> str:
    body: list[str] = []
    for prefix, file_name in TEXTURES:
        image = read_png(ASSETS / file_name)
        body.append(f"// {file_name}: {image.width}x{image.height}")
        body.append(f"inline constexpr float {prefix}_SOURCE_HEIGHT = {image.height}.0f;")
        for part, piece in slice_image(image).items():
            body += render_blob(f"{prefix}_{part}", piece)
        body.append("")
    return "\n".join([
        "// 由 tools/60-gen-bar-textures.py 从 exl/assets/*.png 生成 —— 不要手改，不入库。",
        "",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace sshud::hud {",
        "",
        "struct TextureBlob {",
        "  int32_t width;",
        "  int32_t height;",
        "  const uint8_t* rgba;  // 自下而上的 RGBA32",
        "};",
        "",
        *body,
        "}  // namespace sshud::hud",
        "",
    ])


def selftest() -> int:
    blank, solid = bytes(BYTES_PER_PIXEL), b"\xff" * BYTES_PER_PIXEL
    row = blank * 3 + solid * 34 + blank * 3
    tip = blank + solid + blank + solid * 34 + blank * 3
    image = Image(40, 3, [row, tip, row])
    encoded = b"".join(b"\x00" + r for r in image.rows)
    fixture = PNG_SIGNATURE + b"".join(
        struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
        for kind, body in ((b"IHDR", struct.pack(">IIBBBBB", 40, 3, 8, 6, 0, 0, 0)),
                           (b"IDAT", zlib.compress(encoded)), (b"IEND", b"")))
    tmp = OUT.parent / ".bar_textures_selftest.png"
    tmp.write_bytes(fixture)
    decoded = read_png(tmp)
    tmp.unlink()
    checks = [
        (decoded.rows == image.rows, "PNG 解码"),
        (cap_widths(image) == (3, 3), "端头宽度识别"),
        ([p.width for p in slice_image(image).values()] == [3, MIDDLE_WIDTH, 3], "三段宽度"),
        (raw_rgba_bottom_up(image)[:len(row)] == image.rows[-1], "自下而上存行"),
    ]
    failed = [message for ok, message in checks if not ok]
    for message in failed:
        print(f"  FAIL {message}")
    print("60-gen-bar-textures 自检：" + ("全部通过" if not failed else f"{len(failed)} 项失败"))
    return 1 if failed else 0


def main() -> int:
    if sys.argv[1:] == ["selftest"]:
        return selftest()
    OUT.write_text(render_header(), encoding="utf-8")
    print(f"[✓] {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
