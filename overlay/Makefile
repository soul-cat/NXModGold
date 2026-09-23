# Tesla overlay 构建。产物是 .ovl（本质是 NRO）。
#
# 需要：dkp-pacman -S switch-dev
# 还需要一份【改造版 libtesla】—— 上游 WerWolv/libtesla 做不到常驻 + 输入穿透。
#   git clone https://github.com/ppkantorski/libultrahand vendor/libultrahand
# 放在别处就传 LIBTESLA_DIR=<path>
#
# macOS arm64 用 Docker：
#   docker run --rm -v "$PWD:/work" -w /work devkitpro/devkita64:latest make -C overlay

ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO 未设置。export DEVKITPRO=/opt/devkitpro，或用 devkitpro/devkita64 容器)
endif

include $(DEVKITPRO)/libnx/switch_rules

TARGET   := sshud-overlay
BUILD    := build
SOURCES  := source
INCLUDES := source ../shared

LIBTESLA_DIR ?= $(TOPDIR)/../vendor/libultrahand/libtesla/include
INCLUDES += $(LIBTESLA_DIR)
# tesla.hpp 依赖同仓库 libultra 的 ultra.hpp
INCLUDES += $(TOPDIR)/../vendor/libultrahand/libultra/include
INCLUDES += $(TOPDIR)/../vendor/libultrahand/common
# minizip 无官方包，stub 见 overlay/shim/（不用下载/解压功能）
INCLUDES += $(TOPDIR)/shim

# SaltyNX 的 IPC 头（overlay 侧要用 SaltySD_Connect / GetSharedMemoryHandle）
SALTYNX_SRC ?= $(TOPDIR)/../vendor/SaltyNX/saltysd_core/source
INCLUDES += $(SALTYNX_SRC)

APP_TITLE   := Silksong HUD
APP_AUTHOR  := NXModGold
APP_VERSION := 0.1.0

ARCH     := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS   := -g -Wall -Wextra -O2 -ffunction-sections -fdata-sections $(ARCH) $(DEFINES)
CFLAGS   += $(INCLUDE) -D__SWITCH__
CXXFLAGS := $(CFLAGS) -fno-rtti -fexceptions -std=gnu++20

ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

# libultrahand 头文件链会引入 curl/zlib/mbedtls 符号（即便不用下载功能，
# tesla 部分实现引用了它们），见其 README:109-156
LIBS     := -lcurl -lz -lmbedtls -lmbedx509 -lmbedcrypto -lnx
LIBDIRS  := $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))

export LD := $(CXX)
export OFILES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export APP_TITLE APP_AUTHOR APP_VERSION

.PHONY: all clean check-libtesla

all: check-libtesla $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

# 上游 libtesla 没有 setLayerPos，用它编出来的 overlay 不能常驻。
# 用这个符号的存在与否来判断拿到的是不是改造版。
check-libtesla:
	@test -f $(LIBTESLA_DIR)/tesla.hpp || { \
	  echo ""; \
	  echo "找不到 tesla.hpp（查找路径 $(LIBTESLA_DIR)）"; \
	  echo ""; \
	  echo "需要【改造版】libtesla——上游 WerWolv/libtesla 做不到常驻 + 输入穿透："; \
	  echo "  git clone https://github.com/ppkantorski/libultrahand vendor/libultrahand"; \
	  echo ""; \
	  exit 1; }
	@grep -q 'setLayerPos' $(LIBTESLA_DIR)/tesla.hpp || { \
	  echo ""; \
	  echo "这份 tesla.hpp 里没有 setLayerPos——看起来是上游版本。"; \
	  echo "上游 framebuffer 尺寸写死 448x720 且无法重定位，做不出常驻 HUD。"; \
	  echo "换用 ppkantorski/libultrahand。"; \
	  echo ""; \
	  exit 1; }

$(BUILD):
	@mkdir -p $@

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).ovl $(TARGET).nro $(TARGET).elf $(TARGET).map

else
.PHONY: all
DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).ovl

# .ovl 就是 .nro 改个扩展名——nx-ovlloader 按扩展名识别
$(OUTPUT).ovl: $(OUTPUT).nro
	@cp $< $@
	@echo
	@echo "构建完成: $@"
	@echo
	@echo "安装到 SD 卡:"
	@echo "  switch/.overlays/$(notdir $@)"

$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf: $(OFILES)

%.o: %.cpp
	@echo $(notdir $<)
	@$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(CXXFLAGS) -c $< -o $@

%.o: %.c
	@echo $(notdir $<)
	@$(CC) -MMD -MP -MF $(DEPSDIR)/$*.d $(CFLAGS) -c $< -o $@

-include $(DEPENDS)
endif
