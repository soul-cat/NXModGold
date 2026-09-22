#!/usr/bin/env bash
# 一次性环境准备：devkitPro（交叉编译 ARM64 Switch 目标）+ .NET（跑 Il2CppDumper）
# macOS Apple Silicon。幂等，可重复执行。
set -euo pipefail

BOLD=$'\033[1m'; DIM=$'\033[2m'; GRN=$'\033[32m'; YEL=$'\033[33m'; RED=$'\033[31m'; RST=$'\033[0m'
step() { printf "%s==>%s %s%s%s\n" "$GRN" "$RST" "$BOLD" "$1" "$RST"; }
warn() { printf "%s[!]%s %s\n" "$YEL" "$RST" "$1"; }
die()  { printf "%s[x]%s %s\n" "$RED" "$RST" "$1" >&2; exit 1; }

[ "$(uname -s)" = "Darwin" ] || die "此脚本仅针对 macOS。"
command -v brew >/dev/null || die "未找到 Homebrew，请先安装：https://brew.sh"

VENDOR="$(cd "$(dirname "$0")/.." && pwd)/vendor"
mkdir -p "$VENDOR"

# ---------------------------------------------------------------- devkitPro
step "检查 devkitPro"
if [ -d /opt/devkitpro/devkitA64 ]; then
  printf "  %s已安装：/opt/devkitpro/devkitA64%s\n" "$DIM" "$RST"
else
  warn "devkitPro 未安装。它需要 sudo 且会装到 /opt/devkitpro，请手动执行："
  cat <<'EOF'

  # 1. 下载并安装 dkp-pacman
  curl -L -o /tmp/devkitpro-pacman.pkg \
    https://github.com/devkitPro/pacman/releases/download/v6.0.2/devkitpro-pacman-installer.pkg
  sudo installer -pkg /tmp/devkitpro-pacman.pkg -target /

  # 2. 安装 Switch 工具链
  sudo dkp-pacman -Syu --noconfirm
  sudo dkp-pacman -S --noconfirm switch-dev switch-tools

  # 3. 写入环境变量（zsh）
  cat >> ~/.zshrc <<'ENV'
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=$DEVKITPRO/devkitA64
export PATH=$DEVKITPRO/tools/bin:$DEVKITA64/bin:$PATH
ENV

EOF
  warn "装完后重开终端再跑一次本脚本。"
fi

# ---------------------------------------------------------------- .NET
step "检查 .NET SDK（Il2CppDumper 需要）"
if command -v dotnet >/dev/null; then
  printf "  %s已安装：dotnet %s%s\n" "$DIM" "$(dotnet --version)" "$RST"
else
  warn "未找到 dotnet，正在通过 Homebrew 安装（无需 sudo）…"
  brew install --cask dotnet-sdk
fi

# ---------------------------------------------------------------- 第三方工具
step "拉取分析工具到 vendor/"

clone_or_pull() {
  local url="$1" dir="$2"
  if [ -d "$VENDOR/$dir/.git" ]; then
    printf "  %s更新 %s%s\n" "$DIM" "$dir" "$RST"
    git -C "$VENDOR/$dir" pull --ff-only -q || warn "$dir 更新失败，保留现有版本"
  else
    printf "  %s克隆 %s%s\n" "$DIM" "$dir" "$RST"
    git clone --depth 1 -q "$url" "$VENDOR/$dir"
  fi
}

# Il2CppDumper —— Unity 6000 / metadata v35-v39 分支
clone_or_pull https://github.com/Doppelglower/Il2CppDumper.git Il2CppDumper
# ARM64 内联 hook —— 处理 PC 相对指令重定位，插件直接用
clone_or_pull https://github.com/kyuuseiryuu/And64InlineHook.git And64InlineHook || \
  warn "And64InlineHook 克隆失败，稍后手动处理（见 docs/tasks/）"

step "完成"
cat <<EOF

${BOLD}下一步${RST}
  1. 自己用 nxdumptool 从主机导出 Silksong 的 exefs + romfs
  2. ./tools/10-extract.sh <转储目录>
  3. ./tools/20-dump-il2cpp.sh
  4. ./tools/30-gen-offsets.py

${DIM}本项目不包含也不下载任何游戏数据。转储必须来自你自己的主机与卡带。${RST}
EOF
