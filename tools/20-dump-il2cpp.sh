#!/usr/bin/env bash
# 对 work/ 下的 main NSO + global-metadata.dat 跑 Il2CppDumper，产出 dump.cs / script.json。
#
# 前置：./tools/10-extract.sh <转储目录>
set -euo pipefail

BOLD=$'\033[1m'; DIM=$'\033[2m'; GRN=$'\033[32m'; YEL=$'\033[33m'; RED=$'\033[31m'; RST=$'\033[0m'
step() { printf "%s==>%s %s%s%s\n" "$GRN" "$RST" "$BOLD" "$1" "$RST"; }
warn() { printf "%s[!]%s %s\n" "$YEL" "$RST" "$1"; }
die()  { printf "%s[x]%s %s\n" "$RED" "$RST" "$1" >&2; exit 1; }

REPO="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$REPO/work"
OUT="$WORK/il2cpp"

command -v dotnet >/dev/null || die "未找到 dotnet。跑 ./tools/00-setup.sh，或 brew install --cask dotnet-sdk"

# Il2CppDumper 内部会解压 LZ4 NSO，所以直接喂原始 main.nso 即可，
# 不需要先用 hactool/nstool 解压。
NSO="$WORK/main.nso"
META="$WORK/global-metadata.dat"
[ -f "$NSO" ]  || die "缺少 $NSO —— 先跑 ./tools/10-extract.sh"
[ -f "$META" ] || die "缺少 $META —— 确认转储里包含 RomFS"

# ---------------------------------------------------------------- 定位 dumper
step "定位 Il2CppDumper"
DLL=""
for cand in \
  "$REPO/vendor/Il2CppDumper/Il2CppDumper/bin/Release/net8.0/Il2CppDumper.dll" \
  "$REPO/vendor/Il2CppDumper/Il2CppDumper.dll" \
  "$REPO/vendor/il2cppdumper/Il2CppDumper.dll" \
  "$HOME/tools/il2cppdumper/Il2CppDumper.dll"; do
  [ -f "$cand" ] && { DLL="$cand"; break; }
done

if [ -z "$DLL" ]; then
  warn "找不到 Il2CppDumper.dll。两种取法："
  cat <<'EOF'

  A) 下载跨平台预编译包（最省事）——注意要 net6/net7 的包，"-win-" 的是 Windows 专用：
     gh release download v6.7.46 -R Perfare/Il2CppDumper -p 'Il2CppDumper-net6-v6.7.46.zip'
     unzip Il2CppDumper-net6-v6.7.46.zip -d vendor/il2cppdumper

  B) 从源码构建（vendor/Il2CppDumper 已由 00-setup.sh 克隆）：
     dotnet publish vendor/Il2CppDumper/Il2CppDumper/Il2CppDumper.csproj \
       -c Release -f net8.0 -r osx-arm64 --self-contained

EOF
  exit 1
fi
printf "  %s%s%s\n" "$DIM" "$DLL" "$RST"

# ---------------------------------------------------------------- config.json
# config.json 从 AppDomain.CurrentDomain.BaseDirectory 读取，必须和 dll 同目录。
# RequireAnyKey 必须关掉，否则结束时会卡在 Console.ReadKey 上永不退出。
CONFIG="$(dirname "$DLL")/config.json"
if [ -f "$CONFIG" ]; then
  if grep -q '"RequireAnyKey"[[:space:]]*:[[:space:]]*true' "$CONFIG"; then
    step "关闭 config.json 的 RequireAnyKey（否则非交互运行会卡住）"
    python3 - "$CONFIG" <<'PY'
import json, sys
from pathlib import Path
path = Path(sys.argv[1])
cfg = json.loads(path.read_text(encoding="utf-8"))
cfg["RequireAnyKey"] = False
path.write_text(json.dumps(cfg, indent=2), encoding="utf-8")
print("  已改为 false")
PY
  fi
else
  warn "$CONFIG 不存在。若运行结束后卡住不退出，就是 RequireAnyKey 的问题。"
fi

# ---------------------------------------------------------------- 运行
# 输出目录必须先存在，否则会被静默忽略、产物落到 dll 旁边。
mkdir -p "$OUT"

step "运行 Il2CppDumper"
printf "  %s输入: %s%s\n" "$DIM" "$(basename "$NSO")" "$RST"
printf "  %s      %s%s\n" "$DIM" "$(basename "$META")" "$RST"
printf "  %s输出: %s%s\n" "$DIM" "$OUT" "$RST"

# 参数顺序无所谓：metadata 靠 magic 0xFAB11BAF 识别，目录识别为输出目录。
# DOTNET_ROLL_FORWARD 必须设：预编译包 target net6/net7，而本机通常只有新版运行时。
DOTNET_ROLL_FORWARD=LatestMajor dotnet "$DLL" "$NSO" "$META" "$OUT"

# ---------------------------------------------------------------- 校验
step "校验产物"
[ -f "$OUT/dump.cs" ] || die "没生成 dump.cs，上面应该有报错信息"

TOTAL="$(grep -c 'RVA: ' "$OUT/dump.cs" || true)"
STRIPPED="$(grep -c 'RVA: -1' "$OUT/dump.cs" || true)"
printf "  方法总数     : %s\n" "$TOTAL"
printf "  RVA 为 -1    : %s\n" "$STRIPPED"

if [ "$TOTAL" -gt 0 ]; then
  PCT=$(( STRIPPED * 100 / TOTAL ))
  printf "  占比         : %s%%\n" "$PCT"
  # 抽象方法和泛型本来就会是 -1，个位数百分比正常。
  # 大面积 -1 说明 metadata 解析出问题了，别拿这份 dump 往下做。
  if [ "$PCT" -gt 40 ]; then
    warn "RVA -1 占比过高（${PCT}%），metadata 解析可能出错了。"
    warn "换 Il2CppInspectorRedux 再试一次，别直接用这份 dump。"
  fi
fi

step "确认关键符号"
for sig in 'void TakeDamage(HitInstance' 'HitResponse Hit(HitInstance' 'class HealthManager' 'struct HitInstance'; do
  if grep -q "$sig" "$OUT/dump.cs"; then
    printf "  %s✓%s %s\n" "$GRN" "$RST" "$sig"
  else
    printf "  %s✗%s %s  %s(未找到——可能版本变了)%s\n" "$RED" "$RST" "$sig" "$DIM" "$RST"
  fi
done

BUILD_ID="$(cat "$WORK/BUILD_ID" 2>/dev/null || echo "")"
IMAGE_SIZE="$("$REPO/tools/nsotool.py" info "$NSO" | awk -F': *' '/镜像大小/ {print $2}')"

cat <<EOF

${BOLD}下一步${RST}
  ./tools/30-gen-offsets.py --dump $OUT/dump.cs \\
      --build-id ${BUILD_ID:-<BuildID>} --image-size ${IMAGE_SIZE:-0}

${DIM}手工核对 HealthManager 的字段：${RST}
  grep -n -A30 'class HealthManager' $OUT/dump.cs | head -40
EOF
