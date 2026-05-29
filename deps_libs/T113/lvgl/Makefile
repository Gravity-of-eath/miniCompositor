# Build LVGL 9.0 as a static library (liblvgl9.a) for T113 (armv7, musl).
# Source: ../../../deps_source/T113/lvgl-release-v9.0/src
# Config: ./lv_conf.h (LV_CONF_INCLUDE_SIMPLE)  -- review for T113 LCD size + cache budget.
#
# Native:        make -C deps_libs/T113/lvgl
# Cross (T113):  export CROSS_COMPILE=/develop/toolchain_t113_musl/bin/arm-openwrt-linux-muslgnueabi-
#                make -C deps_libs/T113/lvgl CROSS=1

ifdef CROSS
CC ?= $(CROSS_COMPILE)gcc
AR ?= $(CROSS_COMPILE)ar
endif
CC ?= cc
AR ?= ar

LVGL_SRC_ROOT := ../../../deps_source/T113/lvgl-release-v9.0
SRC_DIR := $(LVGL_SRC_ROOT)/src
OUT_DIR := build
LIB     := liblvgl9.a

CFLAGS := -std=gnu99 -O2 -g -fPIC \
          -Wall -Wno-unused-function -Wno-unused-parameter \
          -Wno-unused-variable -Wno-missing-field-initializers \
          -Wno-implicit-fallthrough -Wno-format \
          -I$(LVGL_SRC_ROOT) \
          -I. -DLV_CONF_INCLUDE_SIMPLE

SRCS := $(shell find $(SRC_DIR) -name '*.c' 2>/dev/null)
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(OUT_DIR)/%.o)

all: $(LIB)

$(LIB): $(OBJS)
	@echo "AR  $@ ($(words $(OBJS)) objs)"
	@$(AR) rcs $@ $(OBJS)

$(OUT_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OUT_DIR) $(LIB)

.PHONY: all clean
