# user set default configuration item 
# if value is None, so value is default value 
import os

# compiled export directory 
OUTPUT_DIR = None

# compile flags
# ..example: OS_FLAGS = " -flag1 -flag2 "
# Note: EGL_FBDEV stays so the EGL config path is selected; the actual
# native window is replaced by ion dma-buf via egl_devices/mc.
OS_FLAGS = " -DEGL_FBDEV -DWITH_GPU_GL -DWITH_NANOVG_GPU -DWITH_GPU_GLES2 -DWITH_LCD_CLEAR_ALPHA -DWITH_FS_RES -DWITH_STB_FONT -DWITH_STB_IMAGE -DHAS_PTHREAD -DWITH_STB_IMAGE -DHAS_STD_MALLOC -DHAS_STDIO -DWITHOUT_CLIPBOARD -DWITH_NULL_IM"

# link flags
# ..example: OS_LINKFLAGS = " -flag1 -flag2 "
OS_LINKFLAGS = " -dynamic "
# OS_LINKFLAGS = " -Wl,--allow-multiple-definition "

# compile libs
# ..example: OS_LIBS = ["lib1", "lib2"]
# OS_LIBS = None "dl","m","pthread", 
OS_LIBS = ["EGL", "GLESv2","spine2d","spine","message","mc"]

# Path to the mc framework root and shared dep dirs. After the
# deps_{libs,source}/ reorg the AWTK source lives at
#   <MC_ROOT>/deps_source/T507/awtk/awtk-linux-fb
# so from awtk-linux-fb/ we step up four levels to reach MC_ROOT.
MC_ROOT  = os.path.abspath("../../../..")
DEPS_LIBS_T507 = os.path.join(MC_ROOT, "deps_libs/T507")

# compile lib paths
# ..example: OS_LIBPATH = ["/path/to/libdir1", "/path/to/libdir2"]
OS_LIBPATH = [
    os.path.join(DEPS_LIBS_T507, "mali"),               # Mali EGL/GLESv2/libmali blobs (was ./lib_t5)
    "./lib",
    "/opt/t5sdk/aarch64-buildroot-linux-gnu/sysroot/lib",
    "/opt/t5sdk/aarch64-buildroot-linux-gnu/sysroot/usr/lib",
    "/opt/t5sdk/aarch64-buildroot-linux-gnu/sysroot/usr/lib64",
    os.path.join(MC_ROOT, "build"),                     # libmc.a
]

# compile include paths
# ..example: OS_CPPPATH = ["/path/to/incdir1", "/path/to/incdir2"]
OS_CPPPATH = [
    "/opt/t5sdk/aarch64-buildroot-linux-gnu/sysroot/usr/include",
    os.path.abspath("./include_t5/libmessage"),
    os.path.join(MC_ROOT, "libmc/include"),
]
0
 
# compile tools prefix CC's name
TOOLS_CC = None

# compile tools prefix CXX's name
TOOLS_CXX = None

# compile tools prefix LD's name
TOOLS_LD = None

# compile tools prefix AR's name
TOOLS_AR = None

# compile tools prefix STRIP's name
TOOLS_STRIP = None

# compile tools prefix RANLIB's name
TOOLS_RANLIB = None

# compile tools prefix path
# ..example: TOOLS_PREFIX = "/path/to/arm-linux-gnueabihf-"
TOOLS_PREFIX = "/develop/toolchain_t507/bin/aarch64-linux-gnu-"
# TOOLS_PREFIX = '/develop/toolchain_t113_musl/bin/arm-openwrt-linux-muslgnueabi-'

# tslib lib path (was ./tslib/T507/lib — now under deps_libs/T507/tslib/)
TSLIB_LIB_DIR = os.path.join(DEPS_LIBS_T507, "tslib/lib")
# tslib include path
TSLIB_INC_DIR = os.path.join(DEPS_LIBS_T507, "tslib/include")
# TSLIB_INC_DIR = None

# enable cursor mouse
# False for touch-only embedded HMI: with mc input thread, touches come
# in as pointer events but should not draw a desktop-style mouse cursor.
ENABLE_CURSOR = False

# null/spinyin/t9/t9ext/pinyin
# ..example: INPUT_ENGINE = "pinyin"
INPUT_ENGINE = None

# NANOVG/NANOVG_PLUS/CAIRO
# ..example: VGCANVAS = "NANOVG"
VGCANVAS = "NANOVG_PLUS"

# awtk's compile is debug
DEBUG = False

# linux's lcd devices type, value is fb/drm/wayland/egl_for_fsl/egl_for_x11/egl_for_gbm/egl_for_wayland/mc
# Note: "mc" replaces "egl_for_t507" on T507 -- still Mali GPU + GLES2,
# but renders into ion dma-bufs owned by mc-compositor so multiple AWTK
# (and LVGL) apps can share the screen with z-order, popups, touch
# routing, lifecycle and bus. The compositor must be running at
# MC_SOCKET (default /tmp/mc.sock) before AWTK starts.
# ..example: LCD_DEVICES = "fb"
LCD_DEVICES = "mc"

# build awtk's linux-fb's tools
BUILD_TOOLS = True

# build awtk's linux-fb's demos
BUILD_DEMOS = True

# build awtk's linux-fb's operation platform
PLATFORM = "linux"

# add extern code list
EXTERN_CODE = None

# enable g2d model 
WITH_G2D = False

# use custom graphic_buffer 
WITH_CUSTOM_GRAPHIC_BUFFER = False

# wayland_scanner path
WAYLAND_SCANNER_PATH = None

