#!/bin/sh
if [ "$1" = "T507" ]; then
    echo "select platform T507"
    cp ./t5_awtk_config_define.py awtk_config_define.py
    scons -c
    scons
elif [ "$1" = "RK3576" ]; then
    echo "select platform RK3576"
    cp ./rk3576_awtk_config_define.py awtk_config_define.py
    scons -c
    scons
elif [ "$1" = "T113" ]; then
    echo "select platform T113"
    cp ./t113_awtk_config_define.py awtk_config_define.py
    scons -c
    scons
elif [ "$1" = "T113-mc" ]; then
    # T113 as an mc-compositor client (software/CPU path, no GPU/EGL).
    # Uses lcd_devices/mc/lcd_mc.c + libmc.a; links against the mc
    # framework built by the top-level Makefile.
    echo "select platform T113-mc (mc_sw, software compositor client)"
    cp ./t113_mc_awtk_config_define.py awtk_config_define.py
    scons -c
    scons
elif [ "$1" = "T113-i" ]; then
    echo "select platform T113-i"
    export STAGING_DIR=/develop/toolchain_t113_i_musl/
    cp ./t113-i_awtk_config_define.py awtk_config_define.py
    scons -c
    scons
else
    echo "select a platform T507 or RK3576 or T113/T113-mc/T113-i"
    echo "Usage: $0 [T507|T113|T113-mc|T113-i]"
fi
