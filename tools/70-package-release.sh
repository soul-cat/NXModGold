#!/usr/bin/env bash
# 把 exl 的构建产物打成可发布的 zip：解压到 SD 卡根目录即可用，也能直接交给 NX-Mod-Manager 添加。
#
# 用法:
#   ./tools/70-package-release.sh 1.0.0      # 先 make -C exl；产物在 work/release/SilksongHUD-v1.0.0.zip
#
# zip 内容:
#   atmosphere/contents/010013C00E930000/exefs/subsdk9
#   atmosphere/contents/010013C00E930000/exefs/main.npdm
#   README.txt
set -euo pipefail

VERSION="${1:?用法: $0 <版本号，如 1.0.0>}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TITLE_ID="010013C00E930000"
MOD_NAME="SilksongHUD"
AUTHOR="cool"
DEPLOY="$REPO_ROOT/exl/deploy/exefs"
OUT_DIR="$REPO_ROOT/work/release"
ZIP="$OUT_DIR/$MOD_NAME-v$VERSION.zip"

for f in subsdk9 main.npdm; do
  [ -f "$DEPLOY/$f" ] || { echo "缺 $DEPLOY/$f —— 先运行 make -C exl"; exit 1; }
done

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
EXEFS="$STAGE/atmosphere/contents/$TITLE_ID/exefs"
mkdir -p "$EXEFS"
cp "$DEPLOY/subsdk9" "$DEPLOY/main.npdm" "$EXEFS/"

cat > "$STAGE/README.txt" <<EOF
$MOD_NAME v$VERSION —— 空洞骑士：丝之歌（Switch）血条与地图收集物
作者：$AUTHOR

功能
  - 怪物血条与伤害数字
  - BOSS 固定血条（整场常驻）
  - 地图收集物标记：没收集才显示，需已买该区域地图
  - 功能开关：游戏「选项 → 游戏」页最后七行；或游戏中按住 R3 + 方向键：↑ 小怪血条  → BOSS 血条  ↓ 伤害数字  ← 地图收集物
  - R3 快捷面板（选项页第五行，默认开）：关掉后按 R3 不再弹面板
  - 送货不损坏（选项页第六行，默认关）：信使送货的物品挨打、撞墙、计时、掉坑都不损坏
  - 新存档起始纹章（选项页第七行，默认猎人）：9 个纹章任选，只影响之后新开的存档
    设置保存在 SD:/config/sshud/config.ini，日志在 SD:/config/sshud/log.txt

要求
  - 大气层（Atmosphère）
  - 游戏版本 v1.0.3（1.0.30000）。其它版本 mod 会自动不生效。

安装（二选一）
  - 手动：把 atmosphere 文件夹解压到 SD 卡根目录
  - NX-Mod-Manager：把本 zip 放进 SD:/mods2/!temp_mods/，在管理器里添加到丝之歌后安装

卸载
  - 删除 SD:/atmosphere/contents/$TITLE_ID/exefs/（同目录的 cheats 金手指不要删）
  - 或在 NX-Mod-Manager 里卸载
EOF

mkdir -p "$OUT_DIR"
rm -f "$ZIP"
(cd "$STAGE" && zip -qr -X "$ZIP" atmosphere README.txt)
echo "[✓] $ZIP"
(cd "$OUT_DIR" && shasum -a 256 "$(basename "$ZIP")")
