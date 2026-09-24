#!/usr/bin/env python3
"""把游戏内开关面板的中文文字离线渲染成白色 RGBA PNG，输出到 exl/assets/menu/<name>.png。

Switch 上 Unity IMGUI 的默认字体没有 CJK 字形，所以面板文字提前画成贴图（入库），再由
tools/60-gen-bar-textures.py 转成 TextureBlob，运行时用 GUI.color 染色。因此 RGB 恒为 255，
抗锯齿覆盖率全部放在 alpha 里。所有图片高度一致（字体 ascent + descent + 上下留白），
逐行对齐时不用再算基线；宽度按实际墨迹裁紧，只留少量左右边距。

仅开发时使用，需要装了 Pillow 的 python（系统 python3 可能没有）。字体用 Noto Sans SC Bold（SIL OFL）：
    curl -fL -o work/fonts/NotoSansSC-Bold.otf \\
        https://github.com/notofonts/noto-cjk/raw/main/Sans/SubsetOTF/SC/NotoSansSC-Bold.otf

用法:
    python3.10 tools/61-render-menu-text.py --font work/fonts/NotoSansSC-Bold.otf
    python3.10 tools/61-render-menu-text.py selftest
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

REPO_ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = REPO_ROOT / "exl" / "assets" / "menu"
DEFAULT_FONT = REPO_ROOT / "work" / "fonts" / "NotoSansSC-Bold.otf"
FONT_PIXEL_SIZE = 32
PAD_X = 2  # 左右留白，防止抗锯齿边缘被裁
PAD_Y = 2  # 上下留白
OPAQUE = 255
WHITE = 255
NOTDEF_PROBE = ""  # 私用区码位，字体里没有，用来取 .notdef（豆腐块）的样子
PROBE_CANVAS = 96
MENU_TEXTS = {
    "title": "显示开关（按住 R3 + 方向键）",
    "enemy_bars": "↑  小怪血条",
    "boss_bar": "→  BOSS 血条",
    "damage_numbers": "↓  伤害数字",
    "map_markers": "←  地图收集物",
    "on": "开",
    "off": "关",
}
ARROW_FALLBACK = {"↑": "▲", "→": "▶", "↓": "▼", "←": "◀"}


def render_glyph_probe(font: ImageFont.FreeTypeFont, char: str) -> bytes:
    """把单个字符画到固定画布上，返回灰度像素，用于和豆腐块比较。"""
    canvas = Image.new("L", (PROBE_CANVAS, PROBE_CANVAS))
    ImageDraw.Draw(canvas).text((PAD_X, PAD_Y), char, font=font, fill=OPAQUE)
    return canvas.tobytes()


def missing_glyphs(font: ImageFont.FreeTypeFont, text: str) -> list[str]:
    """返回字体里缺失（画出来是空白或豆腐块）的非空白字符。"""
    notdef = render_glyph_probe(font, NOTDEF_PROBE)
    missing = []
    for char in sorted(set(text) - {" "}):
        pixels = render_glyph_probe(font, char)
        if pixels == notdef or not any(pixels):
            missing.append(char)
    return missing


def resolve_texts(font: ImageFont.FreeTypeFont) -> dict[str, str]:
    """箭头缺字时换成三角形；换完仍缺字就报错，不产出豆腐块。"""
    texts = dict(MENU_TEXTS)
    missing_arrows = [c for c in missing_glyphs(font, "".join(ARROW_FALLBACK)) if c in ARROW_FALLBACK]
    for name, text in texts.items():
        for arrow in missing_arrows:
            text = text.replace(arrow, ARROW_FALLBACK[arrow])
        texts[name] = text
    if missing_arrows:
        print(f"[!] 字体缺箭头 {''.join(missing_arrows)}，已改用三角形")
    still_missing = missing_glyphs(font, "".join(texts.values()))
    if still_missing:
        raise SystemExit(f"字体缺字：{''.join(still_missing)}")
    return texts


def render_text(font: ImageFont.FreeTypeFont, text: str) -> Image.Image:
    """渲染一行文字：纯白 RGB + 抗锯齿 alpha，高度固定为 ascent + descent + 上下留白。"""
    ascent, descent = font.getmetrics()
    line_height = ascent + descent
    left, top, right, bottom = font.getbbox(text)
    if top < 0 or bottom > line_height:
        raise SystemExit(f"{text!r} 墨迹超出行高 {line_height}：bbox={top}..{bottom}")
    height = line_height + 2 * PAD_Y
    alpha = Image.new("L", (right - left + 2 * PAD_X, height))
    ImageDraw.Draw(alpha).text((PAD_X - left, PAD_Y), text, font=font, fill=OPAQUE)
    # 左边保留笔位起点，各行箭头后的文字才能对齐；右边全角括号等字形带侧边空白，按实际墨迹裁掉
    _, _, ink_right, _ = alpha.getbbox()
    alpha = alpha.crop((0, 0, ink_right + PAD_X, height))
    white = Image.new("L", alpha.size, WHITE)
    return Image.merge("RGBA", (white, white, white, alpha))


def render_all(font_path: Path) -> dict[str, Image.Image]:
    font = ImageFont.truetype(str(font_path), FONT_PIXEL_SIZE)
    return {name: render_text(font, text) for name, text in sorted(resolve_texts(font).items())}


def write_all(font_path: Path) -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for name, image in render_all(font_path).items():
        path = OUT_DIR / f"{name}.png"
        image.save(path, format="PNG", optimize=True)
        print(f"[✓] {path.relative_to(REPO_ROOT)}  {image.width}x{image.height}")
    return 0


def selftest(font_path: Path) -> int:
    images = render_all(font_path)
    heights = {image.height for image in images.values()}
    checks = [(len(heights) == 1, f"高度一致（{sorted(heights)}）")]
    for name, image in images.items():
        red, green, blue, alpha = image.split()
        checks.append((alpha.getbbox() is not None, f"{name} alpha 非空"))
        checks.append((red.getextrema() == green.getextrema() == blue.getextrema() == (WHITE, WHITE),
                       f"{name} RGB 全白"))
    failed = [message for ok, message in checks if not ok]
    for message in failed:
        print(f"  FAIL {message}")
    print("61-render-menu-text 自检：" + ("全部通过" if not failed else f"{len(failed)} 项失败"))
    return 1 if failed else 0


def main() -> int:
    parser = argparse.ArgumentParser(description="渲染开关面板中文文字贴图")
    parser.add_argument("command", nargs="?", choices=("render", "selftest"), default="render")
    parser.add_argument("--font", type=Path, default=DEFAULT_FONT, help="Noto Sans SC Bold 等 CJK 字体")
    args = parser.parse_args()
    if not args.font.is_file():
        raise SystemExit(f"找不到字体 {args.font}（下载方式见脚本说明）")
    return selftest(args.font) if args.command == "selftest" else write_all(args.font)


if __name__ == "__main__":
    sys.exit(main())
