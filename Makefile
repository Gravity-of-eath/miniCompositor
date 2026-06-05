# mc top-level Makefile.
#
# Native (host gcc) build (no LVGL -- prebuilt lib is aarch64-only):
#   make
#
# T507 cross build (everything including LVGL demos):
#   source /develop/toolchain_t5sdk/environment-carbit.sh
#   make CROSS=1                     # compositor + libmc + helpers
#   make CROSS=1 demos               # demo-fullscreen, demo-popup
#   make CROSS=1 MC_ENABLE_EGL=1     # explicit GPU compose (auto-on for T507)
#
# Easiest: ./build.sh T507 (runs both targets + packages output/T507/).
#
# LVGL: liblvgl9.a + lv_conf.h live in ./deps_libs/T507/lvgl/.
#       Source distribution: ./deps_source/T507/lvgl-release-v9.0/.
#       To rebuild the lib:  make -C deps_libs/T507/lvgl CROSS=1

ifdef CROSS
CC      ?= aarch64-linux-gnu-gcc
endif
CC      ?= cc
AR      ?= ar

OUT     := build

# ---- 2D HW accel feature flags ----------------------------------------
#
# Override either with: make MC_ENABLE_G2D=1 MC_ENABLE_RGA=1
# or: make MC_ENABLE_G2D=0 MC_ENABLE_RGA=0 to force CPU-only.
#
# Auto-detect: when CROSS=1 we sniff the toolchain sysroot path for
# vendor markers. The CPU backend is always built regardless.
ifeq ($(MC_ENABLE_G2D),)
  ifneq ($(findstring t5sdk,$(SDKTARGETSYSROOT)),)
    MC_ENABLE_G2D := 1
  endif
  ifneq ($(findstring t507,$(SDKTARGETSYSROOT)),)
    MC_ENABLE_G2D := 1
  endif
  ifneq ($(findstring t113,$(SDKTARGETSYSROOT)),)
    MC_ENABLE_G2D := 1
  endif
  ifneq ($(findstring sun50i,$(SDKTARGETSYSROOT)),)
    MC_ENABLE_G2D := 1
  endif
  ifneq ($(findstring sun8i,$(SDKTARGETSYSROOT)),)
    MC_ENABLE_G2D := 1
  endif
  ifneq ($(findstring sun8iw,$(SDKTARGETSYSROOT)),)
    MC_ENABLE_G2D := 1
  endif
  ifneq ($(findstring allwinner,$(SDKTARGETSYSROOT)),)
    MC_ENABLE_G2D := 1
  endif
endif
ifeq ($(MC_ENABLE_RGA),)
  ifneq ($(findstring rockchip,$(SDKTARGETSYSROOT)),)
    MC_ENABLE_RGA := 1
  endif
  ifneq ($(findstring rv1126,$(SDKTARGETSYSROOT)),)
    MC_ENABLE_RGA := 1
  endif
  ifneq ($(findstring rv1106,$(SDKTARGETSYSROOT)),)
    MC_ENABLE_RGA := 1
  endif
  ifneq ($(findstring rk3036,$(SDKTARGETSYSROOT)),)
    MC_ENABLE_RGA := 1
  endif
  ifneq ($(findstring rk3399,$(SDKTARGETSYSROOT)),)
    MC_ENABLE_RGA := 1
  endif
endif

ACCEL_CFLAGS :=
ACCEL_EXTRA_SRC :=
ACCEL_EXTRA_LDFLAGS :=
ifeq ($(MC_ENABLE_G2D),1)
  ACCEL_CFLAGS    += -DMC_ENABLE_G2D
  ACCEL_EXTRA_SRC += compositor/accel_g2d.c
endif
ifeq ($(MC_ENABLE_RGA),1)
  ACCEL_CFLAGS    += -DMC_ENABLE_RGA
  ACCEL_EXTRA_SRC += compositor/accel_rga.c
endif

# GPU compositor backend (EGL on /dev/fb0 via Mali). Auto-enabled when
# the toolchain sysroot has libEGL + libGLESv2 available (T507 carbit,
# T5SDK). The CPU backends remain compiled in so --backend fb still
# works on systems where Mali isn't accessible.
ifeq ($(MC_ENABLE_EGL),)
  ifneq ($(findstring t5sdk,$(SDKTARGETSYSROOT)),)
    MC_ENABLE_EGL := 1
  endif
  ifneq ($(findstring t507,$(SDKTARGETSYSROOT)),)
    MC_ENABLE_EGL := 1
  endif
endif
ifeq ($(MC_ENABLE_EGL),1)
  ACCEL_CFLAGS        += -DMC_ENABLE_EGL
  ACCEL_EXTRA_SRC     += compositor/backend_egl.c
  ACCEL_EXTRA_LDFLAGS += -lEGL -lGLESv2
  # T5SDK env script sets --sysroot to the libc-only sysroot; EGL/GLESv2
  # headers and libs live in the buildroot sysroot. Add it explicitly
  # so backend_egl.c can find them. CPU-only builds don't need this.
  T5_EGL_SYSROOT := /develop/toolchain_t5sdk/aarch64-buildroot-linux-gnu/sysroot
  ifneq ($(wildcard $(T5_EGL_SYSROOT)/usr/include/EGL/egl.h),)
    ACCEL_CFLAGS        += -I$(T5_EGL_SYSROOT)/usr/include
    ACCEL_EXTRA_LDFLAGS += -L$(T5_EGL_SYSROOT)/usr/lib \
                           -Wl,-rpath-link=$(T5_EGL_SYSROOT)/usr/lib
  endif
endif

# G2D compositor backend (Allwinner sun8i/sun50i). Lets the compositor
# blit each client surface directly into the fb back-buffer via the G2D
# 2D engine instead of CPU memcpy/blend. Auto-enabled on T113 (no Mali,
# so backend_g2d is the only HW compose path). T507 has Mali and uses
# backend_egl, but can still build backend_g2d for benchmarking.
ifeq ($(MC_ENABLE_BACKEND_G2D),)
  ifneq ($(findstring t113,$(SDKTARGETSYSROOT)),)
    MC_ENABLE_BACKEND_G2D := 1
  endif
  ifneq ($(findstring sun8iw,$(SDKTARGETSYSROOT)),)
    MC_ENABLE_BACKEND_G2D := 1
  endif
endif
ifeq ($(MC_ENABLE_BACKEND_G2D),1)
  ACCEL_CFLAGS    += -DMC_ENABLE_BACKEND_G2D
  ACCEL_EXTRA_SRC += compositor/backend_g2d.c
endif

MC_CFLAGS  := -std=gnu99 -Wall -Wextra -Wno-unused-parameter \
              -O2 -g -fPIC -MMD -MP \
              -Icommon -Ilibmc/include $(ACCEL_CFLAGS)
MC_LDFLAGS :=

CFLAGS_ALL  := $(MC_CFLAGS) $(EXTRA_CFLAGS)
LDFLAGS_ALL := $(MC_LDFLAGS) $(EXTRA_LDFLAGS)

# --- core sources ------------------------------------------------------
COMMON_SRC  := common/proto.c

COMP_SRC    := compositor/main.c \
               compositor/transport.c \
               compositor/surface.c \
               compositor/compose.c \
               compositor/backend_fb.c \
               compositor/backend_ppm.c \
               compositor/ial_evdev.c \
               compositor/input.c \
               compositor/bus.c \
               compositor/lifecycle.c \
               compositor/accel_cpu.c \
               compositor/accel_select.c \
               compositor/mc_alloc.c \
               $(ACCEL_EXTRA_SRC) \
               $(COMMON_SRC)

LIBMC_SRC   := libmc/src/connect.c \
               libmc/src/surface.c \
               libmc/src/bus.c \
               libmc/src/toast.c \
               libmc/src/util.c \
               $(COMMON_SRC)

TEST_SRC    := tools/mc-test-client.c
FILL_SRC    := tools/mc-fill-client.c
BUS_SRC     := tools/mc-bus-tool.c
LAUNCHER_SRC := launcher/mc-launcher.c

COMP_OBJ    := $(COMP_SRC:%.c=$(OUT)/%.o)
LIBMC_OBJ   := $(LIBMC_SRC:%.c=$(OUT)/%.o)
TEST_OBJ    := $(TEST_SRC:%.c=$(OUT)/%.o)
FILL_OBJ    := $(FILL_SRC:%.c=$(OUT)/%.o)
BUS_OBJ     := $(BUS_SRC:%.c=$(OUT)/%.o)
LAUNCHER_OBJ := $(LAUNCHER_SRC:%.c=$(OUT)/%.o)

COMP_BIN     := $(OUT)/mc-compositor
LIBMC_A      := $(OUT)/libmc.a
TEST_BIN     := $(OUT)/mc-test-client
FILL_BIN     := $(OUT)/mc-fill-client
BUS_BIN      := $(OUT)/mc-bus-tool
LAUNCHER_BIN := $(OUT)/mc-launcher

# --- LVGL demos --------------------------------------------------------
# LVGL 9.0: source in deps_source/T507/, prebuilt lib + lv_conf in deps_libs/T507/.
# (T507 default; future platforms get their own deps_{source,libs}/<P>/lvgl/.)
LVGL_PLATFORM ?= T507
LVGL_SRC_DIR  := deps_source/$(LVGL_PLATFORM)/lvgl-release-v9.0
LVGL_LIB_DIR  := deps_libs/$(LVGL_PLATFORM)/lvgl
LVGL_INC      := -I$(LVGL_SRC_DIR) -I$(LVGL_LIB_DIR) -DLV_CONF_INCLUDE_SIMPLE -Iports/lvgl
LVGL_LIB      := $(LVGL_LIB_DIR)/liblvgl9.a
LVGL_LIBS     := $(LVGL_LIB) -lm -lpthread

PORT_SRC    := ports/lvgl/lv_port_mc.c
PORT_OBJ    := $(PORT_SRC:%.c=$(OUT)/%.o)

FS_SRC      := examples/demo-fullscreen/main.c
FS_OBJ      := $(FS_SRC:%.c=$(OUT)/%.o)
FS_BIN      := $(OUT)/demo-fullscreen

POP_SRC     := examples/demo-popup/main.c
POP_OBJ     := $(POP_SRC:%.c=$(OUT)/%.o)
POP_BIN     := $(OUT)/demo-popup

# Demos require LVGL flags
$(PORT_OBJ): MC_DEMO_CFLAGS=$(LVGL_INC)
$(FS_OBJ):   MC_DEMO_CFLAGS=$(LVGL_INC)
$(POP_OBJ):  MC_DEMO_CFLAGS=$(LVGL_INC)

# --- default targets ---------------------------------------------------
all: $(COMP_BIN) $(LIBMC_A) $(TEST_BIN) $(FILL_BIN) $(BUS_BIN) $(LAUNCHER_BIN)

demos: $(FS_BIN) $(POP_BIN)

# 纯函数线协议单元测试（宿主机 cc 即可，不依赖交叉/LVGL）
WIRE_TEST_BIN := $(OUT)/mc-toast-wire-test
$(WIRE_TEST_BIN): tools/mc-toast-wire-test.c common/mc_toast_wire.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS_ALL) -o $@ tools/mc-toast-wire-test.c

wire-test: $(WIRE_TEST_BIN)
	$(WIRE_TEST_BIN)

# --- generic compile rule ----------------------------------------------
$(OUT)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_ALL) $(MC_DEMO_CFLAGS) -c $< -o $@

# Pull in auto-generated header dependencies (-MMD -MP). Each .o produces
# a sibling .d listing the headers it included; if any change, the .o
# rebuilds automatically. -MP adds phony targets for the headers so
# renaming/deleting a header doesn't break the build.
-include $(shell find $(OUT) -name "*.d" 2>/dev/null)

# --- link rules --------------------------------------------------------
$(COMP_BIN): $(COMP_OBJ)
	$(CC) $(CFLAGS_ALL) -o $@ $^ $(LDFLAGS_ALL) $(ACCEL_EXTRA_LDFLAGS)

$(LIBMC_A): $(LIBMC_OBJ)
	$(AR) rcs $@ $^

$(TEST_BIN): $(TEST_OBJ) $(LIBMC_A)
	$(CC) $(CFLAGS_ALL) -o $@ $(TEST_OBJ) $(LIBMC_A) $(LDFLAGS_ALL)

$(FILL_BIN): $(FILL_OBJ) $(LIBMC_A)
	$(CC) $(CFLAGS_ALL) -o $@ $(FILL_OBJ) $(LIBMC_A) $(LDFLAGS_ALL)

$(BUS_BIN): $(BUS_OBJ) $(LIBMC_A)
	$(CC) $(CFLAGS_ALL) -o $@ $(BUS_OBJ) $(LIBMC_A) $(LDFLAGS_ALL)

$(LAUNCHER_BIN): $(LAUNCHER_OBJ)
	$(CC) $(CFLAGS_ALL) -o $@ $(LAUNCHER_OBJ) $(LDFLAGS_ALL)

$(FS_BIN): $(FS_OBJ) $(PORT_OBJ) $(LIBMC_A) $(LVGL_LIB)
	$(CC) $(CFLAGS_ALL) -o $@ $(FS_OBJ) $(PORT_OBJ) $(LIBMC_A) $(LVGL_LIBS) $(LDFLAGS_ALL)

$(POP_BIN): $(POP_OBJ) $(PORT_OBJ) $(LIBMC_A) $(LVGL_LIB)
	$(CC) $(CFLAGS_ALL) -o $@ $(POP_OBJ) $(PORT_OBJ) $(LIBMC_A) $(LVGL_LIBS) $(LDFLAGS_ALL)

clean:
	rm -rf $(OUT)

print-config:
	@echo "CC               = $(CC)"
	@echo "SDKTARGETSYSROOT = $(SDKTARGETSYSROOT)"
	@echo "MC_ENABLE_G2D         = $(MC_ENABLE_G2D)"
	@echo "MC_ENABLE_RGA         = $(MC_ENABLE_RGA)"
	@echo "MC_ENABLE_EGL         = $(MC_ENABLE_EGL)"
	@echo "MC_ENABLE_BACKEND_G2D = $(MC_ENABLE_BACKEND_G2D)"
	@echo "LVGL_PLATFORM         = $(LVGL_PLATFORM)"
	@echo "ACCEL_CFLAGS          = $(ACCEL_CFLAGS)"
	@echo "ACCEL_EXTRA_SRC       = $(ACCEL_EXTRA_SRC)"
	@echo "CFLAGS_ALL       = $(CFLAGS_ALL)"
	@echo "LDFLAGS_ALL      = $(LDFLAGS_ALL)"
	@echo "LVGL_LIB         = $(LVGL_LIB)"

.PHONY: all clean print-config demos wire-test
