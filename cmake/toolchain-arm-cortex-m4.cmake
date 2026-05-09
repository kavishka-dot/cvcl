# cmake/toolchain-arm-cortex-m4.cmake
#
# Cross-compilation toolchain for ARM Cortex-M4 (with FPU)
# Targets: STM32F4xx, STM32F7xx, nRF52840, i.MX RT1060, etc.
#
# Usage:
#   cmake -B build-m4 \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-cortex-m4.cmake \
#     -DCVCL_NO_SIMD=ON \
#     -DCVCL_NO_STDLIB=ON \
#     -DCVCL_BUILD_TESTS=OFF \
#     -DCVCL_BUILD_EXAMPLES=OFF
#
# Requirements:
#   arm-none-eabi-gcc (arm-gnu-toolchain or Homebrew arm-none-eabi)
#
# Install on Ubuntu:  sudo apt-get install gcc-arm-none-eabi
# Install on macOS:   brew install --cask gcc-arm-embedded
# Install on Windows: https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# -------------------------------------------------------------------------
# Toolchain programs
# -------------------------------------------------------------------------
set(TOOLCHAIN_PREFIX "arm-none-eabi-")

find_program(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}gcc   REQUIRED)
find_program(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++   REQUIRED)
find_program(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc   REQUIRED)
find_program(CMAKE_AR           ${TOOLCHAIN_PREFIX}ar    REQUIRED)
find_program(CMAKE_OBJCOPY      ${TOOLCHAIN_PREFIX}objcopy)
find_program(CMAKE_OBJDUMP      ${TOOLCHAIN_PREFIX}objdump)
find_program(CMAKE_SIZE         ${TOOLCHAIN_PREFIX}size)

# -------------------------------------------------------------------------
# CPU flags -- Cortex-M4 with FPU (hard-float ABI)
# -------------------------------------------------------------------------
set(CPU_FLAGS
    "-mcpu=cortex-m4"
    "-mthumb"
    "-mfpu=fpv4-sp-d16"
    "-mfloat-abi=hard"
)
string(JOIN " " CPU_FLAGS_STR ${CPU_FLAGS})

# -------------------------------------------------------------------------
# Compile/link flags
# -------------------------------------------------------------------------
set(COMMON_FLAGS "${CPU_FLAGS_STR} -ffunction-sections -fdata-sections -fno-exceptions")

set(CMAKE_C_FLAGS_INIT          "${COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT        "${COMMON_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT        "${COMMON_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${CPU_FLAGS_STR} -Wl,--gc-sections -specs=nosys.specs -specs=nano.specs")

# Release: optimize for size (MCU flash is limited)
set(CMAKE_C_FLAGS_RELEASE_INIT   "-Os -DNDEBUG")
set(CMAKE_C_FLAGS_DEBUG_INIT     "-Og -g3 -DDEBUG")
set(CMAKE_C_FLAGS_MINSIZEREL_INIT "-Os -DNDEBUG")

# -------------------------------------------------------------------------
# Sysroot -- tell CMake not to look for host libraries
# -------------------------------------------------------------------------
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# -------------------------------------------------------------------------
# CVCL-specific defaults for this target
# -------------------------------------------------------------------------
# Cortex-M4 has NEON-like DSP but not full NEON -- disable SIMD
# Freestanding mode required (no libc on bare metal)
set(CVCL_NO_SIMD   ON  CACHE BOOL "Disable SIMD for Cortex-M4" FORCE)
set(CVCL_NO_STDLIB ON  CACHE BOOL "Freestanding mode for Cortex-M4" FORCE)
set(CVCL_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(CVCL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CVCL_BUILD_BENCH    OFF CACHE BOOL "" FORCE)

message(STATUS "CVCL toolchain: ARM Cortex-M4 (hard-float, freestanding)")
