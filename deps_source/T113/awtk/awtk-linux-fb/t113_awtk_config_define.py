# user set default configuration item 
# if value is None, so value is default value 
import os

# compiled export directory 
OUTPUT_DIR = None

# compile flags
# ..example: OS_FLAGS = " -flag1 -flag2 "
OS_FLAGS = (
    " -DWITH_LCD_CLEAR_ALPHA -DWITH_FS_RES -DWITH_STB_FONT -DWITH_STB_IMAGE"
    " -DHAS_PTHREAD -DHAS_STD_MALLOC -DHAS_STDIO"
    " -DWITHOUT_CLIPBOARD -DWITH_NULL_IM"
    " -DWITHOUT_FSCRIPT=1 -DWITHOUT_INPUT_METHOD=1 -DWITHOUT_XML_STYLE=1"
    " -ffunction-sections -fdata-sections"
    " -fno-asynchronous-unwind-tables -fno-unwind-tables"
)

# link flags
# ..example: OS_LINKFLAGS = " -flag1 -flag2 "
OS_LINKFLAGS = " -Wl,--gc-sections -Wl,--as-needed -Wl,-s "
# OS_LINKFLAGS = " -Wl,--allow-multiple-definition "

# compile libs
# ..example: OS_LIBS = ["lib1", "lib2"]
# OS_LIBS = None "dl","m","pthread",
# OS_LIBS = ["EGL", "GLESv2"]
#OS_LIBS = ["ts"]

# compile lib paths
# ..example: OS_LIBPATH = ["/path/to/libdir1", "/path/to/libdir2"]
OS_LIBPATH = ["/develop/toolchain_t113_musl/lib","/develop/toolchain_t113_musl/lib32","/develop/toolchain_t113_musl/lib64"]

# compile include paths
# ..example: OS_CPPPATH = ["/path/to/incdir1", "/path/to/incdir2"]
OS_CPPPATH = ["/develop/toolchain_t113_musl/include"]

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
TOOLS_PREFIX = "/develop/toolchain_t113_musl/bin/arm-openwrt-linux-muslgnueabi-"


# tslib lib path
# ..example: TSLIB_LIB_DIR = "/path/to/tslib/lib"
#TSLIB_LIB_DIR = os.path.abspath("./tslib/T113/lib")

# tslib include path
# ..example: TSLIB_INC_DIR = "/path/to/tslib/include"
#TSLIB_INC_DIR = os.path.abspath("./tslib/T113/include")

# enable cursor mouse
ENABLE_CURSOR = True

# null/spinyin/t9/t9ext/pinyin
# ..example: INPUT_ENGINE = "pinyin"
INPUT_ENGINE = "null"

# NANOVG/NANOVG_PLUS/CAIRO
# ..example: VGCANVAS = "NANOVG"
VGCANVAS = "NANOVG"

# awtk's compile is debug
DEBUG = False

# linux's lcd devices type, value is fb/drm/wayland/egl_for_fsl/egl_for_x11/egl_for_gbm/egl_for_wayland
# ..example: LCD_DEVICES = "fb"
LCD_DEVICES = "fb"

# build awtk's linux-fb's tools
BUILD_TOOLS = True

# build awtk's linux-fb's demos
BUILD_DEMOS = False

# build awtk's linux-fb's operation platform
PLATFORM = None

# add extern code list
EXTERN_CODE = None

# enable g2d model 
WITH_G2D = False

# use custom graphic_buffer 
WITH_CUSTOM_GRAPHIC_BUFFER = False

# wayland_scanner path
WAYLAND_SCANNER_PATH = None

