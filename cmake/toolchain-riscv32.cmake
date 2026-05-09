# cmake/toolchain-riscv32.cmake
#
# Cross-compilation toolchain for RISC-V 32-bit (bare-metal)
# Targets: ESP32-C3, GD32VF103, SiFive FE310, Kendryte K210
#
# Usage:
#   cmake -B build-rv32 \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv32.cmake \
#     -DCVCL_NO_SIMD=ON \
#     -DCVCL_NO_STDLIB=ON \
#     -DCMAKE_BUILD_TYPE=MinSizeRel
#
# Requirements:
#   riscv32-unknown-elf-gcc or riscv-none-elf-gcc
#
# Install: sudo apt-get install gcc-riscv64-linux-gnu
#   (use riscv64 toolchain with -march=rv32imc for 32-bit output)
# Or download from: https://github.com/riscv-collab/riscv-gnu-toolchain

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)

# -------------------------------------------------------------------------
# Toolchain
# -------------------------------------------------------------------------
# Try riscv32 prefix first, then riscv-none-elf (newer toolchain naming)
find_program(CMAKE_C_COMPILER   riscv32-unknown-elf-gcc)
if(NOT CMAKE_C_COMPILER)
    find_program(CMAKE_C_COMPILER riscv-none-elf-gcc REQUIRED)
endif()

get_filename_component(TOOLCHAIN_DIR ${CMAKE_C_COMPILER} DIRECTORY)
get_filename_component(TOOLCHAIN_NAME ${CMAKE_C_COMPILER} NAME_WE)
string(REPLACE "-gcc" "" TOOLCHAIN_PREFIX ${TOOLCHAIN_NAME})

find_program(CMAKE_AR      ${TOOLCHAIN_PREFIX}-ar)
find_program(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}-objcopy)
find_program(CMAKE_SIZE    ${TOOLCHAIN_PREFIX}-size)

# -------------------------------------------------------------------------
# CPU flags -- RV32IMFC (int + mul + compressed + float)
# Adjust march to match your target:
#   rv32imc    -- no float (M0-equivalent, software float)
#   rv32imfc   -- single-precision float
#   rv32imfdc  -- double-precision float
# -------------------------------------------------------------------------
set(MARCH "rv32imfc")
set(MABI  "ilp32f")

set(CPU_FLAGS "-march=${MARCH} -mabi=${MABI}")

set(CMAKE_C_FLAGS_INIT          "${CPU_FLAGS} -ffunction-sections -fdata-sections")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${CPU_FLAGS} -Wl,--gc-sections -nostartfiles")

set(CMAKE_C_FLAGS_RELEASE_INIT    "-Os -DNDEBUG")
set(CMAKE_C_FLAGS_MINSIZEREL_INIT "-Os -DNDEBUG")
set(CMAKE_C_FLAGS_DEBUG_INIT      "-Og -g3 -DDEBUG")

# -------------------------------------------------------------------------
# Sysroot
# -------------------------------------------------------------------------
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# -------------------------------------------------------------------------
# CVCL defaults
# -------------------------------------------------------------------------
set(CVCL_NO_SIMD   ON  CACHE BOOL "No SIMD on bare-metal RISC-V" FORCE)
set(CVCL_NO_STDLIB ON  CACHE BOOL "Freestanding mode"            FORCE)
set(CVCL_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(CVCL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CVCL_BUILD_BENCH    OFF CACHE BOOL "" FORCE)

message(STATUS "CVCL toolchain: RISC-V 32-bit bare-metal (march=${MARCH}, freestanding)")
