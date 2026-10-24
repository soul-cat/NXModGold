#!/usr/bin/env python3
"""把 SilkRuntime 皮肤包重打包成能与 SilksongHUD 共存的 TOMOD/手动安装包。

只改容器布局，不改皮肤运行时的二进制、资源或 IPS：

  exefs/subsdk9  ->  exefs/subsdk8   （皮肤模块必须排在 HUD 的 subsdk9 之前）
  exefs/main.npdm ->  union main.npdm（与 HUD 包逐字节相同，TOMOD 按引用计数共享）
  romfs/...       ->  原样
  exefs_patches/  ->  原样

用法：
    ./tools/85-repack-skin-coexist.py \
        --skin-zip <原始皮肤包.zip> \
        --npdm <由 tools/40-patch-npdm.py 生成的 union main.npdm> \
        --out work/release/SilksongSkin-coexist.zip

原始包里的皮肤资源属于原作者/移植者；本脚本只做路径与权限文件的替换。
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import sys
import tempfile
import zipfile
from pathlib import Path

TITLE_ID = "010013C00E930000"
CONTENTS = f"atmosphere/contents/{TITLE_ID}"
SKIN_EXEFS = f"{CONTENTS}/exefs"
SKIN_LOADER = f"{SKIN_EXEFS}/subsdk9"
COEXIST_LOADER = f"{SKIN_EXEFS}/subsdk8"
SKIN_NPDM = f"{SKIN_EXEFS}/main.npdm"
SKIN_ROOT = f"{CONTENTS}/romfs/SilkModLoader/Mods/Skin"
IPS_DIR = "atmosphere/exefs_patches/SilkModLoader"

README = """SilksongSkin-coexist —— 与 SilksongHUD 共存的皮肤包

这个包只改了文件布局，皮肤运行时（SilkModLoader）、资源和 IPS 都来自原始皮肤包：
  - 皮肤：终焉苍龙（B 站），Switch 移植：nice2cu093
  - SilkModLoader / SilkRuntime：https://github.com/nice2cu1/SilkRuntime（GPL-2.0）
  - 重打包：cool（SilksongHUD）

布局：
  exefs/subsdk8        皮肤运行时（原包是 subsdk9，改名后排在 HUD 的 subsdk9 之前）
  exefs/main.npdm      与 SilksongHUD 包逐字节相同的 union npdm（TOMOD 引用计数共享）
  romfs/...            原皮肤资源
  exefs_patches/...    原皮肤 IPS；其目标地址仍是 main + 0x8A3A000 + hook_offset

安装（TOMOD 或手动均可）：
  1. 先装 SilksongHUD（exefs/subsdk9 + 同一份 main.npdm），或先装本包，顺序不限；
  2. 两个包的文件集不重叠，唯一共享的是逐字节相同的 main.npdm；
  3. 不要再用商店里的原始皮肤包，也不要让它自动更新（会覆盖回 subsdk9/旧 npdm）。

要求：游戏 v1.0.3（Build ID FC9EA4CCC955D579）、Atmosphère。
"""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def validate_skin_zip(entries: set[str]) -> None:
    if SKIN_LOADER not in entries:
        sys.exit(f"皮肤包缺少 {SKIN_LOADER}，这不是预期的 SilkRuntime 皮肤包")
    if SKIN_NPDM not in entries:
        sys.exit(f"皮肤包缺少 {SKIN_NPDM}，这不是预期的 SilkRuntime 皮肤包")
    if not any(name.startswith(IPS_DIR + "/") and name.endswith(".ips") for name in entries):
        sys.exit(f"皮肤包缺少 {IPS_DIR}/*.ips")
    skin_dirs = {
        name[len(SKIN_ROOT) + 1:].split("/", 1)[0]
        for name in entries
        if name.startswith(SKIN_ROOT + "/")
    }
    skin_dirs.discard("")
    if len(skin_dirs) != 1:
        sys.exit(f"皮肤包应当只有一个皮肤目录，实际: {sorted(skin_dirs)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skin-zip", required=True, type=Path)
    parser.add_argument("--npdm", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()

    if not args.skin_zip.is_file():
        sys.exit(f"找不到皮肤包: {args.skin_zip}")
    if not args.npdm.is_file():
        sys.exit(f"找不到 npdm: {args.npdm}")
    npdm = args.npdm.read_bytes()
    if not npdm.startswith(b"META"):
        sys.exit(f"{args.npdm} 不是 npdm（缺 META 魔数）")

    with zipfile.ZipFile(args.skin_zip) as archive:
        names = {info.filename for info in archive.infolist() if not info.is_dir()}
        validate_skin_zip(names)

    with tempfile.TemporaryDirectory(prefix="silksong-skin-") as temp:
        root = Path(temp)
        with zipfile.ZipFile(args.skin_zip) as archive:
            archive.extractall(root)

        loader = root / COEXIST_LOADER
        loader.parent.mkdir(parents=True, exist_ok=True)
        os.replace(root / SKIN_LOADER, loader)
        (root / SKIN_NPDM).write_bytes(npdm)
        (root / "COEXIST-README.txt").write_text(README, encoding="utf-8")

        files = []
        for directory, _, filenames in os.walk(root):
            for filename in filenames:
                path = Path(directory) / filename
                files.append(path.relative_to(root).as_posix())
        files.sort()

        args.out.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for name in files:
                archive.write(root / name, name)

    print(f"[✓] {args.out}（{args.out.stat().st_size} 字节）")
    print(f"    npdm sha256: {sha256(args.npdm)}")
    with zipfile.ZipFile(args.out) as archive:
        names = [info.filename for info in archive.infolist() if not info.is_dir()]
    print(f"    文件数: {len(names)}")
    print(f"    {COEXIST_LOADER}: {SKIN_LOADER} -> 已改名")
    print(f"    {SKIN_NPDM}: 已替换为 union npdm")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
