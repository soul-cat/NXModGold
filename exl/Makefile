# exlaunch 版构建：产出 exefs 注入用的 subsdk9 + main.npdm。
#
# Ryujinx 与 Atmosphère 用同一份产物：
#   Ryujinx    : 右键游戏 →「打开 Atmosphere Mods 目录」，放到 <该目录>/sshud/exefs/
#   Atmosphère : SD:/atmosphere/contents/010013C00E930000/exefs/
#
# 需要：devkitPro（switch-dev）、vendor/exlaunch（见 HANDOFF）、
#       plugin/source/offsets.h 与 work/dump/ExeFS/main.npdm（都来自你自己的转储）。
#
# vendor/exlaunch 是模板仓库，程序代码要放进它的 source/program/。
# 为了不改 vendor，每次构建把它同步到 build/sshud/，再把 program/ 和
# config.mk 覆盖进去，并应用 patches/ 下的修补。

ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO 未设置。执行 export DEVKITPRO=/opt/devkitpro)
endif

REPO_ROOT := $(abspath $(CURDIR)/..)
EXLAUNCH  ?= $(REPO_ROOT)/vendor/exlaunch
STAGE     := $(CURDIR)/build/sshud
OFFSETS   := $(REPO_ROOT)/plugin/source/offsets.h
GAME_NPDM ?= $(REPO_ROOT)/work/dump/ExeFS/main.npdm
DEPLOY    := $(CURDIR)/deploy/exefs

export PATH := $(DEVKITPRO)/tools/bin:$(DEVKITPRO)/devkitA64/bin:$(PATH)

.PHONY: all subsdk stage check clean

all: subsdk
	@test -f $(GAME_NPDM) || { \
	  echo ""; \
	  echo "subsdk9 已编译，但缺 $(GAME_NPDM)，不组装 deploy/。"; \
	  echo "exlaunch 需要替换 main.npdm 才能写 hook，且必须基于游戏原版 npdm："; \
	  echo "  ./tools/05-unpack-container.sh --keys <prod.keys> <base.nsp> <update.nsz>"; \
	  echo ""; \
	  exit 1; }
	@rm -rf $(DEPLOY) && mkdir -p $(DEPLOY)
	@cp $(STAGE)/deploy/subsdk9 $(DEPLOY)/subsdk9
	@$(REPO_ROOT)/tools/40-patch-npdm.py patch --in $(GAME_NPDM) --out $(DEPLOY)/main.npdm
	@echo ""
	@echo "产物: $(DEPLOY)/{subsdk9,main.npdm}"
	@echo "  Ryujinx    : <Atmosphere Mods 目录>/sshud/exefs/"
	@echo "  Atmosphère : SD:/atmosphere/contents/010013C00E930000/exefs/"

subsdk: check stage
	@$(MAKE) --no-print-directory -C $(STAGE) REPO_ROOT=$(REPO_ROOT)

check:
	@test -d $(EXLAUNCH)/source/lib || { \
	  echo "缺 $(EXLAUNCH)：git clone --depth 1 https://github.com/shadowninja108/exlaunch.git vendor/exlaunch"; \
	  exit 1; }
	@test -f $(OFFSETS) || { \
	  echo "缺 $(OFFSETS) —— 它由游戏转储生成，不在版本库里（见 HANDOFF 流水线 05→10→20→30）。"; \
	  exit 1; }

# 只同步 vendor；build/ 与 deploy/ 被排除，因而保留增量编译的中间产物
stage:
	@mkdir -p $(STAGE)
	@rsync -a --delete --exclude .git --exclude /source/program --exclude /build --exclude /deploy \
	  $(EXLAUNCH)/ $(STAGE)/
	@rsync -a --delete $(CURDIR)/program/ $(STAGE)/source/program/
	@cp $(CURDIR)/config.mk $(STAGE)/config.mk
	@# 对 exlaunch 的修补（每次 stage 都从 vendor 原样同步，所以总是打在干净文件上）
	@for p in $(CURDIR)/patches/*.patch; do patch -s -p1 -d $(STAGE) < $$p || exit 1; done

clean:
	@rm -rf $(CURDIR)/build $(CURDIR)/deploy
