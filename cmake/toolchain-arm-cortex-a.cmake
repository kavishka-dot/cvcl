# cmake/toolchain-arm-cortex-a.cmake
#
# Cross-compilation toolchain for ARM Cortex-A (Linux userspace)
# Targets: Raspberry Pi 3/4/5, NVIDIA Jetson, i.MX 8, BeagleBone AI
#
# Usage:
#   cmake -B build-a \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-cortex-a.cmake \
#     -DCMAKE_BUILD_TYPE=Release
#
# Requirements:
#   aarch64-linux-gnu-gcc (for 64-bit) or arm-linux-gnueabihf-gcc (32-bit)
#
# Install: sudo apt-get install gcc-aarch64-linux-gnu
#
# Note: Cortex-A Linux targets have a full libc, so CVCL_NO_STDLIB is NOT
# set by default. NEON SIMD is available -- CVCL_NO_SIMD stays OFF.

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# -------------------------------------------------------------------------
# Toolchain -- prefer aarch64 (64-bit), fallback to armhf (32-bit)
# -------------------------------------------------------------------------
set(TOOLCHAIN_PREFIX "aarch64-linux-gnu-")

find_program(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}gcc REQUIRED)
find_program(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++ REQUIRED)
find_program(CMAKE_AR           ${TOOLCHAIN_PREFIX}ar)
find_program(CMAKE_OBJCOPY      ${TOOLCHAIN_PREFIX}objcopy)
find_program(CMAKE_SIZE         ${TOOLCHAIN_PREFIX}size)

# -------------------------------------------------------------------------
# CPU flags -- generic AArch64, tuned for Cortex-A53 (Pi 3/4 compatible)
# -------------------------------------------------------------------------
set(CPU_FLAGS "-march=armv8-a -mtune=cortex-a53")

set(CMAKE_C_FLAGS_INIT         "${CPU_FLAGS}")
set(CMAKE_C_FLAGS_RELEASE_INIT "${CPU_FLAGS} -O3 -ffast-math")
set(CMAKE_C_FLAGS_DEBUG_INIT   "${CPU_FLAGS} -Og -g3")

# -------------------------------------------------------------------------
# Sysroot
# -------------------------------------------------------------------------
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# -------------------------------------------------------------------------
# CVCL defaults for this target
# -------------------------------------------------------------------------
# Cortex-A has NEON -- keep SIMD on. Has full libc -- no_stdlib off.
set(CVCL_NO_SIMD   OFF CACHE BOOL "NEON available on Cortex-A" FORCE)
set(CVCL_NO_STDLIB OFF CACHE BOOL "Full libc available"        FORCE)

message(STATUS "CVCL toolchain: ARM Cortex-A AArch64 (NEON, full libc)")
