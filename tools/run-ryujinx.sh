#!/usr/bin/env bash
# 用项目内的数据目录启动 Ryujinx（keys、固件、存档、mods 都在 work/ryujinx/）。
# 直接双击 Ryujinx.app 会用 ~/Library/Application Support/Ryujinx，那里是空的。
#
# 用法:
#   ./tools/run-ryujinx.sh              # 优先 datas/Ryujinx.app，其次 vendor 里自建的
#   RYUJINX_APP=<path> ./tools/run-ryujinx.sh
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
DATA="$REPO/work/ryujinx"

APP="${RYUJINX_APP:-}"
if [ -z "$APP" ]; then
  for cand in "$REPO/datas/Ryujinx.app" "$REPO/vendor/ryubing-build/output_arm64/Ryujinx.app"; do
    if [ -d "$cand" ]; then APP="$cand"; break; fi
  done
fi
[ -n "$APP" ] || { echo "找不到 Ryujinx.app（放到 datas/ 或用 RYUJINX_APP 指定）" >&2; exit 1; }
[ -f "$DATA/system/prod.keys" ] || { echo "缺 $DATA/system/prod.keys" >&2; exit 1; }

# 注意：日志不跟随数据目录，Ryujinx 在 macOS 上固定写 ~/Library/Logs/Ryujinx/
open -n "$APP" --args --root-data-dir "$DATA"
