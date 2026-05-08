cmake_minimum_required(VERSION 3.16)

project(cvcl
    VERSION 0.1.0
    DESCRIPTION "High-performance Computer Vision C Library"
    LANGUAGES C
)

# -------------------------------------------------------------------------
# Language standard
# -------------------------------------------------------------------------
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# -------------------------------------------------------------------------
# Options
# -------------------------------------------------------------------------
option(CVCL_BUILD_TESTS    "Build unit tests"            ON)
option(CVCL_BUILD_EXAMPLES "Build example programs"      ON)
option(CVCL_BUILD_BENCH    "Build benchmarks"            ON)
option(CVCL_WITH_STB       "Enable PNG/JPEG via stb_image" OFF)
option(CVCL_NO_SIMD        "Disable SIMD acceleration"   OFF)
option(CVCL_SANITIZE       "Enable AddressSanitizer"     OFF)
option(CVCL_COVERAGE       "Enable code coverage"        OFF)

# -------------------------------------------------------------------------
# Compiler flags
# -------------------------------------------------------------------------
add_library(cvcl_compile_options INTERFACE)

if(MSVC)
    target_compile_options(cvcl_compile_options INTERFACE
        /W4 /wd4996 /wd4100
        # /WX  # uncomment to treat warnings as errors on MSVC
    )
else()
    target_compile_options(cvcl_compile_options INTERFACE
        -Wall -Wextra -Wpedantic -Werror
        -Wno-unused-parameter
        -fno-strict-aliasing
    )
endif()

if(CVCL_SANITIZE)
    target_compile_options(cvcl_compile_options INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(cvcl_compile_options INTERFACE
        -fsanitize=address,undefined)
endif()

if(CVCL_COVERAGE)
    target_compile_options(cvcl_compile_options INTERFACE --coverage)
    target_link_options(cvcl_compile_options INTERFACE --coverage)
endif()

# Release: aggressive optimization
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(cvcl_compile_options INTERFACE
        -O3 -march=native -ffast-math)
endif()

# -------------------------------------------------------------------------
# Source files
# -------------------------------------------------------------------------
set(CVCL_SOURCES
    src/core/alloc.c
    src/core/error.c
    src/core/image.c
    src/io/io_ppm.c
    src/io/io_stb.c
    src/io/io_png_native.c
    src/filter/median.c
    src/draw/draw.c
    src/transform/resize.c
    src/filter/blur.c
    src/filter/convolve.c
    src/filter/edge.c
    src/filter/morph.c
    src/core/pixel_hist.c
    src/core/integral.c
    src/core/connected_components.c
)

# -------------------------------------------------------------------------
# Library target
# -------------------------------------------------------------------------
add_library(cvcl STATIC ${CVCL_SOURCES})

target_include_directories(cvcl
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(cvcl
    PRIVATE cvcl_compile_options
)

# Math library: built into MSVC CRT, explicit on Linux/macOS
if(NOT WIN32)
    target_link_libraries(cvcl PUBLIC m)
endif()

if(CVCL_NO_SIMD)
    target_compile_definitions(cvcl PUBLIC CVCL_NO_SIMD)
endif()

if(CVCL_WITH_STB)
    # Check stb headers exist in third_party/
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb_image.h")
        message(FATAL_ERROR
            "CVCL_WITH_STB=ON but third_party/stb_image.h not found.\n"
            "See third_party/README.md for download instructions.")
    endif()
    target_compile_definitions(cvcl PUBLIC CVCL_WITH_STB)
    target_include_directories(cvcl PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party>
    )
endif()

# -------------------------------------------------------------------------
# Tests
# -------------------------------------------------------------------------
if(CVCL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# -------------------------------------------------------------------------
# Examples
# -------------------------------------------------------------------------
if(CVCL_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# -------------------------------------------------------------------------
# Benchmarks
# -------------------------------------------------------------------------
if(CVCL_BUILD_BENCH)
    add_subdirectory(bench)
endif()

# -------------------------------------------------------------------------
# Install rules
# -------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS cvcl cvcl_compile_options
    EXPORT cvclTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/cvcl
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT cvclTargets
    FILE cvclTargets.cmake
    NAMESPACE cvcl::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cvcl
)

# -------------------------------------------------------------------------
# Package config
# -------------------------------------------------------------------------
include(CMakePackageConfigHelpers)

configure_package_config_file(
    cmake/cvclConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/cvclConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cvcl
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/cvclConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/cvclConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/cvclConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cvcl
)
