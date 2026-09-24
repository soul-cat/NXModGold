#!/usr/bin/env bash
# 从 XCI / NSP（含 NSZ/XCZ 压缩容器）解出偏移流水线需要的两样东西：
#   ExeFS 里的 main（NSO）与 RomFS 里的 global-metadata.dat，
#   组装成 10-extract.sh 可直接消费的 work/dump/ 目录。
#
# 前提（材料必须来自你自己的主机与游戏）：
#   - prod.keys（Lockpick 导出）；NSP 自带 ticket 时自动取 titlekey
#   - 多个容器时取版本最高者作偏移来源（实机跑的是最新版，版本不匹配
#     则 Build ID 对不上，插件端 verifyBuildId 会拒绝加载）
#
# 用法:
#   ./tools/05-unpack-container.sh --keys <prod.keys> <base.nsp|base.xci> [update.nsz|update.nsp ...]
#
# 选项:
#   --keys <path>   prod.keys 路径（默认依次找 work/prod.keys、~/switch/prod.keys）
#   --keep          保留中间产物（默认成功后清掉，只留 work/dump/）
# 注意：不设 pipefail —— find|head 是本脚本的核心惯用法，pipefail 会在
# find 被 head 提前关闭管道（SIGPIPE）时误杀脚本；关键步骤均以产物校验代替退出码。
set -eu

BOLD=$'\033[1m'; DIM=$'\033[2m'; GRN=$'\033[32m'; YEL=$'\033[33m'; RED=$'\033[31m'; RST=$'\033[0m'
step() { printf "%s==>%s %s%s%s\n" "$GRN" "$RST" "$BOLD" "$1" "$RST"; }
warn() { printf "%s[!]%s %s\n" "$YEL" "$RST" "$1"; }
die()  { printf "%s[x]%s %s\n" "$RED" "$RST" "$1" >&2; exit 1; }

REPO="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$REPO/work"
CONT="$WORK/containers"
NSOTOOL="$REPO/tools/nsotool.py"
TITLE_BASE="010013c00e930000"   # Silksong 基础版
TITLE_UPD="010013c00e930800"    # Silksong 更新档

KEYS=""
KEEP=0
INPUTS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --keys) KEYS="$2"; shift 2 ;;
    --keep) KEEP=1; shift ;;
    -h|--help) sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    -*) die "未知选项: $1（支持 --keys <path> / --keep）" ;;
    *) INPUTS+=("$1"); shift ;;
  esac
done
[ "${#INPUTS[@]}" -ge 1 ] || die "用法: $0 [--keys <prod.keys>] [--keep] <base.nsp|base.xci> [update.nsz ...]"

# ---------------------------------------------------------------- 密钥
if [ -z "$KEYS" ]; then
  for cand in "$WORK/prod.keys" "$HOME/switch/prod.keys"; do
    if [ -f "$cand" ]; then KEYS="$cand"; break; fi
  done
fi
[ -n "$KEYS" ] && [ -f "$KEYS" ] || die "找不到 prod.keys（用 --keys 指定，或放到 work/prod.keys）"
KEYS="$(cd "$(dirname "$KEYS")" && pwd)/$(basename "$KEYS")"
grep -q '^header_key' "$KEYS" || warn "prod.keys 未见 header_key 条目，格式可能不对，解密大概率会失败"
printf "  %skeyset: %s%s\n" "$DIM" "$KEYS" "$RST"

# ---------------------------------------------------------------- nstool
NSTOOL="$REPO/vendor/nstool/nstool"
if [ ! -x "$NSTOOL" ]; then
  step "下载 nstool（jakcron/nstool 预编译版）"
  case "$(uname -sm)" in
    "Darwin arm64")  ASSET="macos_arm64" ;;
    "Darwin x86_64") ASSET="macos_x86_64" ;;
    *) die "本机无预编译包（$(uname -sm)），请自行编译 nstool 放到 vendor/nstool/nstool" ;;
  esac
  mkdir -p "$REPO/vendor/nstool"
  curl -fsSL -o /tmp/nstool.zip "https://github.com/jakcron/nstool/releases/download/v1.9.2/nstool-v1.9.2-$ASSET.zip"
  (cd "$REPO/vendor/nstool" && unzip -oq /tmp/nstool.zip && chmod +x nstool)
fi

# ---------------------------------------------------------------- nsz 解压工具（仅压缩容器需要）
NSZ="$WORK/nszenv/bin/nsz"
ensure_nsz() {
  if [ ! -x "$NSZ" ]; then
    step "准备 nsz 解压工具（首次创建 venv 并安装依赖）"
    python3 -m venv "$WORK/nszenv"
    "$WORK/nszenv/bin/pip" install --quiet nsz
  fi
}

# ---------------------------------------------------------------- 逐个容器拆包
# 对每个输入产出: C_VER(版本整数) C_PROG(程序NCA路径) C_EXEFS(含 main 的 ExeFS 目录)
mkdir -p "$CONT"
# nstool 的 --tik 是数组参数：把所有输入的 ticket 一并传入，这样增量更新 NCA
# 在内部重建基础版 NCA 时也能解密（它需要 base 的 titlekey）。
declare -a ALL_TIK=()   # 所有输入的 ticket 路径（nstool 可多 --tik）
declare -a TK_LINES=()  # title.keys 行: <RightsId大写>:<加密titlekey>:<世代>
declare -a C_VER=() C_PROG=() C_EXEFS=()
n_ok=0

i=0
for f in "${INPUTS[@]}"; do
  abs="$(cd "$(dirname "$f")" && pwd)/$(basename "$f")"
  [ -f "$abs" ] || die "找不到文件: $f"
  ext="$(basename "$abs" | tr '[:upper:]' '[:lower:]' | sed 's/.*\.//')"
  stage="$CONT/$i"
  rm -rf "$stage"; mkdir -p "$stage"
  C_VER[$i]=0; C_PROG[$i]=""; C_EXEFS[$i]=""

  # 压缩容器先解回普通容器（nstool 只认 xci/nsp）
  src="$abs"
  case "$ext" in
    nsz|xcz)
      ensure_nsz
      case "$ext" in nsz) outext="nsp" ;; xcz) outext="xci" ;; esac
      stem="$(basename "$abs")"; stem="${stem%.*}"
      step "[$i] 解压 ${ext} → ${outext}（zstd 压缩容器，输出约数 GB，稍等）"
      cp "$abs" "$stage/$stem.$ext"
      (cd "$stage" && "$NSZ" -D --keys "$KEYS" "$stem.$ext")
      rm -f "$stage/$stem.$ext"
      src="$stage/$stem.$outext"
      [ -f "$src" ] || die "nsz 解压失败，未见输出 $src"
      ;;
    nsp|xci) ;;
    *) warn "[$i] 未知扩展名 ${ext}，按容器格式尝试" ;;
  esac

  # 拆开容器，拿到扁平的 NCA / ticket / cert
  root="$stage/root"
  step "[$i] 展开 ${ext} 容器"
  case "$src" in
    *.nsp|*.NSP) "$NSTOOL" -k "$KEYS" -x "$root" "$src" >/dev/null ;;
    *.xci|*.XCI) "$NSTOOL" -k "$KEYS" --secure "$root" "$src" >/dev/null ;;
    *) die "[$i] 无法处理的容器: $src" ;;
  esac

  tik="$(find "$root" -name '*.tik' 2>/dev/null | head -1)"
  cert="$(find "$root" -name '*.cert' 2>/dev/null | head -1)"
  tikargs=()
  if [ -n "$tik" ];  then tikargs+=(--tik "$tik"); ALL_TIK+=("$tik"); fi
  if [ -n "$cert" ]; then tikargs+=(--cert "$cert"); fi

  # 从 ticket 提取加密 titlekey + 密钥世代，供 LibHac(ncaext) 使用
  if [ -n "$tik" ]; then
    tk_view=(-t tik)
    if [ -n "$cert" ]; then tk_view+=(--cert "$cert"); fi
    tkinfo="$("$NSTOOL" -k "$KEYS" ${tk_view[@]+"${tk_view[@]}"} "$tik" 2>/dev/null)"
    tkey="$(printf '%s\n' "$tkinfo" | awk '/Title Key/{f=1} f && /Data:/{getline; gsub(/ /,""); print tolower($0); exit}')"
    tgen="$(printf '%s\n' "$tkinfo" | grep -m1 'Key Generation:' | grep -o '[0-9]\+' | head -1)"
    if [ -n "$tkey" ] && [ -n "$tgen" ]; then
      rid="$(basename "$tik" .tik | tr '[:lower:]' '[:upper:]')"
      TK_LINES+=("$rid:$tkey:$tgen")
      printf "  %s[%s] titlekey 已提取: %s (gen %s)%s\n" "$DIM" "$i" "$rid" "$tgen" "$RST"
    fi
  fi

  # 元数据 NCA → cnmt：给出标题 ID、版本号、程序 NCA 的内容 ID
  prog_id=""
  meta_nca="$(find "$root" -name '*.cnmt.nca' 2>/dev/null | head -1)"
  if [ -n "$meta_nca" ]; then
    "$NSTOOL" -k "$KEYS" -x /0 "$stage/meta" "$meta_nca" >/dev/null 2>&1
    cnmt="$(find "$stage/meta" -name '*.cnmt' 2>/dev/null | head -1)"
    [ -n "$cnmt" ] || die "[$i] meta NCA 里没找到 .cnmt"
    cnmt_info="$("$NSTOOL" -k "$KEYS" -t cnmt "$cnmt" 2>/dev/null)"
    tid="$(printf '%s\n' "$cnmt_info" | grep -m1 'TitleId:' | awk '{print $2}' | sed 's/^0x//' | tr '[:upper:]' '[:lower:]')"
    C_VER[$i]="$(printf '%s\n' "$cnmt_info" | grep -m1 '^  Version:' | grep -o '(v[0-9]*)' | tr -d 'v()')"
    prog_id="$(printf '%s\n' "$cnmt_info" | grep -A1 'Type: *Program' | grep -m1 'Id:' | awk '{print $2}')"
    [ -n "$tid" ] || die "[$i] cnmt 解析不出 TitleId"
    case "$tid" in
      "$TITLE_BASE"|"$TITLE_UPD") ;;
      *) die "[$i] 标题 ID 不符: ${tid}（期望 $TITLE_BASE 或 ${TITLE_UPD}）——这不是 Silksong 的转储" ;;
    esac
  else
    warn "[$i] 没找到 meta NCA，版本按 0 处理"
  fi
  printf "  %s[%s] 版本 %s%s\n" "$DIM" "$i" "${C_VER[$i]}" "$RST"

  # 定位程序 NCA：优先 cnmt 指名，退回逐个解 part0 找 NSO0 的 main
  step "[$i] 定位程序 NCA（ExeFS）"
  exefs=""
  if [ -n "$prog_id" ] && [ -f "$root/$prog_id.nca" ]; then
    cand="$root/$prog_id.nca"
    exefs="$stage/exefs"
    "$NSTOOL" -k "$KEYS" ${tikargs[@]+"${tikargs[@]}"} -x /0 "$exefs" "$cand" >/dev/null 2>&1 \
      || die "[$i] 程序 NCA 解密失败: $(basename "$cand")"
  else
    warn "[$i] cnmt 未指明程序 NCA，退回扫描"
    for nca in $(find "$root" -name '*.nca' ! -name '*.cnmt.nca' 2>/dev/null); do
      p0="$stage/p0/$(basename "$nca" .nca)"
      if ! "$NSTOOL" -k "$KEYS" ${tikargs[@]+"${tikargs[@]}"} -x /0 "$p0" "$nca" >/dev/null 2>&1; then
        continue   # 解不开的（delta 片段等）跳过
      fi
      if [ -f "$p0/main" ] && [ "$(head -c 4 "$p0/main" 2>/dev/null)" = "NSO0" ]; then
        cand="$nca"; exefs="$p0"; break
      fi
    done
  fi

  if [ -n "$exefs" ] && [ -f "$exefs/main" ] && [ "$(head -c 4 "$exefs/main" 2>/dev/null)" = "NSO0" ]; then
    C_PROG[$i]="$cand"; C_EXEFS[$i]="$exefs"
    printf "  %s[%s] 程序 NCA: %s%s\n" "$DIM" "$i" "$(basename "$cand")" "$RST"
    printf "  %s[%s] Build ID: %s%s\n" "$DIM" "$i" "$("$NSOTOOL" buildid "$exefs/main")" "$RST"
    n_ok=$((n_ok+1))
  else
    warn "[$i] 没解出含 main NSO 的程序 NCA，此输入作废"
  fi

  i=$((i+1))
done

[ "$n_ok" -ge 1 ] || die "没有任何输入解出程序 NCA"

# ---------------------------------------------------------------- 选版本最高的输入
best=0; best_ver=-1
i=0
for f in "${INPUTS[@]}"; do
  if [ -n "${C_PROG[$i]}" ] && [ "${C_VER[$i]}" -ge "$best_ver" ]; then
    best=$i; best_ver="${C_VER[$i]}"
  fi
  i=$((i+1))
done
step "采用输入 [$best]（版本 ${best_ver}）作为偏移来源"

# 更新档 NCA 的 RomFS 常为增量分区，需要基础版程序 NCA 才能重建
base_nca=""
i=0
for f in "${INPUTS[@]}"; do
  if [ "$i" -ne "$best" ] && [ -n "${C_PROG[$i]}" ]; then
    base_nca="${C_PROG[$i]}"
  fi
  i=$((i+1))
done

# ---------------------------------------------------------------- 解 RomFS（LibHac / ncaext）
# nstool 不支持固件 16+ 的 NCA 压缩分区（Silksong 的 RomFS 用了），也不支持
# 更新档的增量重建链，所以 RomFS 阶段用 tools/ncaext（基于 LibHac，与模拟器同源）。
prog="${C_PROG[$best]}"
if [ -n "$base_nca" ]; then
  nca_base="$base_nca"; nca_upd="$prog"
else
  nca_base="$prog"; nca_upd="-"
fi

step "定位 dotnet（ncaext 依赖 .NET 8+）"
DOTNET_BIN="$(command -v dotnet || true)"
if [ -z "$DOTNET_BIN" ] && [ -x "$HOME/.dotnet/dotnet" ]; then DOTNET_BIN="$HOME/.dotnet/dotnet"; fi
[ -n "$DOTNET_BIN" ] || die "未找到 dotnet。免 sudo 安装：curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 8.0 --install-dir ~/.dotnet"

TITLEKEYS="$CONT/title.keys"
: > "$TITLEKEYS"
for line in ${TK_LINES[@]+"${TK_LINES[@]}"}; do printf '%s\n' "$line" >> "$TITLEKEYS"; done

step "构建 ncaext（LibHac 提取器，tools/ncaext）"
NCAEXT_DIR="$REPO/tools/ncaext"
NCAEXT_OUT="$WORK/ncaext"
[ -f "$NCAEXT_DIR/ncaext.csproj" ] || die "缺少 ${NCAEXT_DIR}（仓库不完整）"
"$DOTNET_BIN" build "$NCAEXT_DIR" -c Release --output "$NCAEXT_OUT" -v q >/dev/null

step "解出 RomFS（压缩分区 + 增量重建，整树导出，可能需要几分钟）"
romfs="$CONT/romfs"
rm -rf "$romfs"; mkdir -p "$romfs"
"$NCAEXT_OUT/ncaext" "$KEYS" "$TITLEKEYS" "$nca_base" "$nca_upd" "$romfs" --extract-all
meta_dat="$(find "$romfs" -iname 'global-metadata.dat' | head -1)"
[ -n "$meta_dat" ] || die "RomFS 里没找到 global-metadata.dat（IL2CPP 元数据）"
printf "  %s%s%s\n" "$DIM" "$meta_dat" "$RST"

# ---------------------------------------------------------------- 组装 work/dump/
DUMP="$WORK/dump"
step "组装转储目录"
rm -rf "$DUMP"
mkdir -p "$DUMP/ExeFS" "$DUMP/RomFS"
cp "${C_EXEFS[$best]}/main" "$DUMP/ExeFS/main"
# main.npdm：exl/ 构建要在它基础上补 SVC（tools/40-patch-npdm.py）
if [ -f "${C_EXEFS[$best]}/main.npdm" ]; then
  cp "${C_EXEFS[$best]}/main.npdm" "$DUMP/ExeFS/main.npdm"
else
  warn "exefs 里没有 main.npdm —— exl/ 版无法组装 deploy/"
fi
# exl/ 版以 subsdk9 注入；游戏若自带同名模块会被覆盖
[ -f "${C_EXEFS[$best]}/subsdk9" ] && warn "游戏 exefs 自带 subsdk9，exl/ 版会与它冲突"
printf "  %sexefs: %s%s\n" "$DIM" "$(ls "${C_EXEFS[$best]}" | tr '\n' ' ')" "$RST"
cp "$meta_dat" "$DUMP/RomFS/global-metadata.dat"
ls -lh "$DUMP/ExeFS" "$DUMP/RomFS" | sed 's/^/  /'

if [ "$KEEP" -eq 0 ]; then
  step "清理中间产物（--keep 可保留）"
  rm -rf "$CONT"
fi

cat <<EOF

${BOLD}下一步${RST}
  ./tools/10-extract.sh work/dump

${DIM}注意：偏移只对这个 Build ID 的版本有效。实机装的若不是同一版本，
插件端 verifyBuildId 会拒绝加载 —— 到时用对应版本的转储重跑本脚本即可。${RST}
EOF
