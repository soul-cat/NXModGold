#!/usr/bin/env bash
# 用项目内的数据目录启动 Ryujinx（keys、固件、存档、mods、日志都在 datas/portable/）。
# datas/Ryujinx.app 直接双击也行：macOS 上 Ryujinx 会自动使用 .app 旁边的
# portable/ 目录（AppDataManager.Initialize）。本脚本主要给 vendor 里的自建版用。
#
# 用法:
#   ./tools/run-ryujinx.sh              # 优先 datas/Ryujinx.app，其次 vendor 里自建的
#   RYUJINX_APP=<path> ./tools/run-ryujinx.sh
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
DATA="$REPO/datas/portable"

APP="${RYUJINX_APP:-}"
if [ -z "$APP" ]; then
  for cand in "$REPO/datas/Ryujinx.app" "$REPO/vendor/ryubing-build/output_arm64/Ryujinx.app"; do
    if [ -d "$cand" ]; then APP="$cand"; break; fi
  done
fi
[ -n "$APP" ] || { echo "找不到 Ryujinx.app（放到 datas/ 或用 RYUJINX_APP 指定）" >&2; exit 1; }
[ -f "$DATA/system/prod.keys" ] || { echo "缺 $DATA/system/prod.keys" >&2; exit 1; }

# .app 旁边就是 portable/ 时走 portable 模式（日志写 $DATA/Logs/）；
# 否则用 --root-data-dir，但该模式下日志固定写 ~/Library/Logs/Ryujinx/
if [ "$(cd "$(dirname "$APP")" && pwd)/portable" = "$DATA" ]; then
  open -n "$APP"
else
  open -n "$APP" --args --root-data-dir "$DATA"
fi
