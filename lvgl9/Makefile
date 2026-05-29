# Build LVGL 9.0 as a static library (libforlvgl9.a).
# Sources come from ../lvgl-release-v9.0/src (recursive .c globbing).
# Config from ./lv_conf.h (LV_CONF_INCLUDE_SIMPLE).
#
# Native build:    make -C lvgl9
# Cross for T507:  source /develop/toolchain_t5sdk/environment-carbit.sh
#                  make -C lvgl9 CROSS=1

ifdef CROSS
CC ?= aarch64-linux-gnu-gcc
AR ?= aarch64-linux-gnu-ar
endif
CC ?= cc
AR ?= ar

SRC_DIR := ../lvgl-release-v9.0/src
OUT_DIR := build
LIB     := liblvgl9.a

CFLAGS := -std=gnu99 -O2 -g -fPIC \
          -Wall -Wno-unused-function -Wno-unused-parameter \
          -Wno-unused-variable -Wno-missing-field-initializers \
          -Wno-implicit-fallthrough -Wno-format \
          -I../lvgl-release-v9.0 \
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
