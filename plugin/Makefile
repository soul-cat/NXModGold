# SaltyNX 插件构建。
#
# 产物是【裸 .elf】—— 不是 .nro 也不是 .nso，没有 elf2nro/nacptool 步骤。
# 直接丢到 SD 卡的 SaltySD/plugins/<TITLEID>/ 下即可。
#
# 需要：dkp-pacman -S switch-dev
# macOS arm64 上若 devkitPro 原生包有问题，用 Docker：
#   docker run --rm -v "$PWD:/work" -w /work devkitpro/devkita64:latest make -C plugin

ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO 未设置。执行 export DEVKITPRO=/opt/devkitpro，或用 devkitpro/devkita64 容器)
endif

include $(DEVKITPRO)/libnx/switch_rules

# 工具链前缀（switch_rules 提供 PORTLIBS 等，但工具名自己拼）
TOOLPREFIX := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-
SIZE       := $(TOOLPREFIX)size
STRIP      := $(TOOLPREFIX)strip

TARGET   := sshud
BUILD    := build
SOURCES  := source
INCLUDES := source ../shared shim

# SaltyNX 的头文件路径。默认指向 vendor/ 下克隆的仓库；
# 放在别处就在命令行传 SALTYNX_SRC=<path>。
SALTYNX_SRC ?= $(TOPDIR)/../vendor/SaltyNX/saltysd_core/source
INCLUDES += $(SALTYNX_SRC)

# -fPIE 必需：插件被加载到任意地址。
# -fno-plt 避免 PLT 间接跳转，简化符号解析。
ARCH     := -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE -fno-plt

CFLAGS   := -g -Wall -Wextra -O2 -ffunction-sections -fdata-sections $(ARCH) $(DEFINES)
CFLAGS   += $(INCLUDE) -DSWITCH -D__SWITCH__
# 无异常、无 RTTI：插件跑在游戏进程里，不能引入 unwinder
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17

ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=$(TOPDIR)/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
# 加载器按名字要求导出 main
LDFLAGS  += -Wl,--require-defined=main -Wl,--dynamic-list=$(TOPDIR)/exports.txt
LDFLAGS  += -Wl,-z,max-page-size=0x1000

LIBS     := -lnx
LIBDIRS  := $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

export LD := $(CXX)
export OFILES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: all clean check-offsets

all: check-offsets $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

# offsets.h 由 tools/30-gen-offsets.py 从你自己的游戏转储生成，不入库。
check-offsets:
	@test -f $(CURDIR)/source/offsets.h || { \
	  echo ""; \
	  echo "缺少 source/offsets.h —— 它由游戏转储生成，不在版本库里。"; \
	  echo ""; \
	  echo "  ./tools/10-extract.sh <转储目录>"; \
	  echo "  ./tools/20-dump-il2cpp.sh"; \
	  echo "  ./tools/30-gen-offsets.py"; \
	  echo ""; \
	  exit 1; }

$(BUILD):
	@mkdir -p $@

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).map

else
.PHONY: all
DEPENDS := $(OFILES:.o=.d)

# 产物就是 .elf —— SaltyNX 直接加载它
all: $(OUTPUT).elf

$(OUTPUT).elf: $(OFILES)
	@echo linking $(notdir $@)
	@$(LD) $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS) -o $@
	@echo
	@echo "构建完成: $@"
	@$(NM) $@ | grep -q ' T main' || { echo "错误: main 未导出"; exit 1; }
	@$(STRIP) $@ -o $@.stripped; \
	 SIZE=$$(wc -c < $@.stripped | tr -d ' '); \
	 echo "  strip 后大小: $$SIZE 字节"; \
	 rm -f $@.stripped; \
	 if [ $$SIZE -gt 2097152 ]; then \
	   echo "  错误: 超过 SaltyNX 的 2 MiB 上限"; exit 1; \
	 fi
	@echo
	@echo "安装到 SD 卡:"
	@echo "  SaltySD/plugins/010013C00E930000/$(notdir $@)"

%.o: %.cpp
	@echo $(notdir $<)
	@$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(CXXFLAGS) -c $< -o $@

%.o: %.c
	@echo $(notdir $<)
	@$(CC) -MMD -MP -MF $(DEPSDIR)/$*.d $(CFLAGS) -c $< -o $@

%.o: %.s
	@echo $(notdir $<)
	@$(CC) -MMD -MP -MF $(DEPSDIR)/$*.d -x assembler-with-cpp $(ASFLAGS) -c $< -o $@

-include $(DEPENDS)
endif
