# aarch64-linux-gnu-toolchain.cmake
# Cross-compilation toolchain configuration for aarch64-linux-gnu
# Generated from environment-carbit.sh

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# set(PATH "/develop/toolchain_t507/bin" ${PATH})
# Specify the cross compiler
set(CMAKE_C_COMPILER /develop/toolchain_t507/bin/aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER /develop/toolchain_t507/bin/aarch64-linux-gnu-g++)

# Sysroot location
set(SDKTARGETSYSROOT "/develop/toolchain_t507/aarch64-linux-gnu/libc")
set(CMAKE_SYSROOT ${SDKTARGETSYSROOT})
set(CMAKE_FIND_ROOT_PATH ${SDKTARGETSYSROOT})

# Compiler flags
set(CMAKE_C_FLAGS " -O2 -pipe -fPIC -g -I/develop/toolchain_t507/carbit_utils/include" CACHE STRING "C flags")
set(CMAKE_CXX_FLAGS "-fPIC -std=c++11 -O2 -pipe -g -I/develop/toolchain_t507/carbit_utils/include" CACHE STRING "C++ flags")
set(CMAKE_CPP_FLAGS " -g" CACHE STRING "CPP flags")
set(CMAKE_EXE_LINKER_FLAGS "-L/develop/toolchain_t507/carbit_utils/lib  -lpthread -llog" CACHE STRING "Executable linker flags")
set(CMAKE_MODULE_LINKER_FLAGS "-L/develop/toolchain_t507/carbit_utils/lib  -lpthread -llog" CACHE STRING "Module linker flags")
set(CMAKE_SHARED_LINKER_FLAGS "-L/develop/toolchain_t507/carbit_utils/lib  -lpthread -llog" CACHE STRING "Shared linker flags")

# Additional libraries
set(LIBS_ADDITIONAL_DECODE " -L/develop/toolchain_t507/decoder -lMemAdapter -lvdecoder -lVE -lcdc_base -lvideoengine -lcdx_common -lcdx_base -lsdk_memory -lcdx_ion -lsdk_disp -lsdk_decoder")

# Search paths
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_INSTALL_PREFIX "${PWD}/install")

# Cross-compilation tools
set(CMAKE_AS_COMPILER /develop/toolchain_t507/bin/aarch64-linux-gnu-as)
set(CMAKE_LINKER /develop/toolchain_t507/bin/aarch64-linux-gnu-ld)
set(CMAKE_AR /develop/toolchain_t507/bin/aarch64-linux-gnu-ar)
set(CMAKE_NM /develop/toolchain_t507/bin/aarch64-linux-gnu-nm)
set(CMAKE_OBJCOPY /develop/toolchain_t507/bin/aarch64-linux-gnu-objcopy)
set(CMAKE_OBJDUMP /develop/toolchain_t507/bin/aarch64-linux-gnu-objdump)
set(CMAKE_RANLIB /develop/toolchain_t507/bin/aarch64-linux-gnu-ranlib)
set(CMAKE_STRIP /develop/toolchain_t507/bin/aarch64-linux-gnu-strip)

# Additional include and library paths
include_directories(
    /develop/toolchain_t507/carbit_utils/include
    ${SDKTARGETSYSROOT}/usr/include
)

link_directories(
    /develop/toolchain_t507/carbit_utils/lib
    /develop/toolchain_t507/decoder
    ${SDKTARGETSYSROOT}/usr/lib
    ${SDKTARGETSYSROOT}/lib
)

# Cross-compilation variables
set(ARCH arm64)
# set(CROSS_COMPILE aarch64-linux-gnu-)
set(STAGING_DIR .)