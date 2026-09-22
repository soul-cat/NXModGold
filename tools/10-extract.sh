#!/usr/bin/env bash
# 从你自己的 Silksong 转储中定位并暂存分析所需文件。
#
# 推荐转储方式（不需要在 Mac 上放 prod.keys）：
#   在主机上用 nxdumptool，选 Silksong →「NCA / Ticket / RomFS / ExeFS options」
#   →「ExeFS section data」和「RomFS section data」分别导出到 SD。
#   然后把 SD 上的 switch/nxdumptool/ExeFS 和 RomFS 目录拷到 Mac。
#
# 用法:
#   ./tools/10-extract.sh <转储根目录>
set -euo pipefail

BOLD=$'\033[1m'; DIM=$'\033[2m'; GRN=$'\033[32m'; YEL=$'\033[33m'; RED=$'\033[31m'; RST=$'\033[0m'
step() { printf "%s==>%s %s%s%s\n" "$GRN" "$RST" "$BOLD" "$1" "$RST"; }
warn() { printf "%s[!]%s %s\n" "$YEL" "$RST" "$1"; }
die()  { printf "%s[x]%s %s\n" "$RED" "$RST" "$1" >&2; exit 1; }

REPO="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$REPO/work"
TITLE_ID="010013C00E930000"

[ $# -ge 1 ] || die "用法: $0 <转储根目录>"
SRC="$1"
[ -d "$SRC" ] || die "找不到目录: $SRC"

mkdir -p "$WORK"

# ---------------------------------------------------------------- 找 main NSO
step "定位 main NSO"
MAIN=""
while IFS= read -r cand; do
  [ -f "$cand" ] || continue
  if [ "$(head -c 4 "$cand")" = "NSO0" ]; then MAIN="$cand"; break; fi
done < <(find "$SRC" -type f \( -name main -o -name 'main.nso' \) 2>/dev/null)

[ -n "$MAIN" ] || die "在 $SRC 下没找到 main NSO。确认导出的是 ExeFS section data。"
printf "  %s%s%s\n" "$DIM" "$MAIN" "$RST"
cp -f "$MAIN" "$WORK/main.nso"

# ---------------------------------------------------------------- Build ID
step "读取 Build ID"
BUILD_ID="$("$REPO/tools/nsotool.py" buildid "$WORK/main.nso")"
printf "  %sBuild ID: %s%s%s\n" "$DIM" "$BOLD" "$BUILD_ID" "$RST"
echo "$BUILD_ID" > "$WORK/BUILD_ID"

"$REPO/tools/nsotool.py" info "$WORK/main.nso" | sed 's/^/  /'

# ---------------------------------------------------------------- 解压
step "解压 NSO 成扁平镜像"
"$REPO/tools/nsotool.py" decompress "$WORK/main.nso" -o "$WORK/main.bin" | sed 's/^/  /'

# ---------------------------------------------------------------- global-metadata
step "定位 global-metadata.dat"
META="$(find "$SRC" -type f -name 'global-metadata.dat' 2>/dev/null | head -1)"
if [ -n "$META" ]; then
  printf "  %s%s%s\n" "$DIM" "$META" "$RST"
  cp -f "$META" "$WORK/global-metadata.dat"
  # 元数据版本在头部 0x04 处（u32）
  VER="$(python3 -c "
import struct,sys
d=open(sys.argv[1],'rb').read(8)
print('magic=%s version=%d' % (d[:4].hex().upper(), struct.unpack_from('<I',d,4)[0]))
" "$WORK/global-metadata.dat")"
  printf "  %s%s%s\n" "$DIM" "$VER" "$RST"
  printf "  %s期望 magic=BAF11BFA version=31（Unity 6000.0.x）%s\n" "$DIM" "$RST"
  printf "  %s若 version >= 35，上游 Il2CppDumper 不支持，需要换分支%s\n" "$DIM" "$RST"
else
  warn "没找到 global-metadata.dat。"
  warn "它在 RomFS 里，路径类似 Data/il2cpp_data/Metadata/global-metadata.dat"
  warn "确认你也导出了 RomFS section data。"
fi

# ---------------------------------------------------------------- 汇总
step "暂存完成"
ls -lh "$WORK" | sed 's/^/  /'
cat <<EOF

${BOLD}下一步${RST}
  ./tools/20-dump-il2cpp.sh

${DIM}注意：work/ 已被 .gitignore 排除，游戏数据不会进入版本库。${RST}
EOF
