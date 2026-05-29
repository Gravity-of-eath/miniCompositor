# Build LVGL 9.0 as a static library (liblvgl9.a).
# Source: ../../../deps_source/T507/lvgl-release-v9.0/src
# Config: ./lv_conf.h (LV_CONF_INCLUDE_SIMPLE)
#
# Native:        make -C deps_libs/T507/lvgl
# Cross (T507):  source /develop/toolchain_t5sdk/environment-carbit.sh
#                make -C deps_libs/T507/lvgl CROSS=1

ifdef CROSS
CC ?= aarch64-linux-gnu-gcc
AR ?= aarch64-linux-gnu-ar
endif
CC ?= cc
AR ?= ar

LVGL_SRC_ROOT := ../../../deps_source/T507/lvgl-release-v9.0
SRC_DIR := $(LVGL_SRC_ROOT)/src
OUT_DIR := build
LIB     := liblvgl9.a

CFLAGS := -std=gnu99 -O2 -g -fPIC \
          -Wall -Wno-unused-function -Wno-unused-parameter \
          -Wno-unused-variable -Wno-missing-field-initializers \
          -Wno-implicit-fallthrough -Wno-format \
          -I$(LVGL_SRC_ROOT) \
          -I. -DLV_CONF_INCLUDE_SIMPLE

SRCS := $(shell find $(SRC_DIR) -name '*.c')
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
