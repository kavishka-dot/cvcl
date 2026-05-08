cmake_minimum_required(VERSION 3.16)

add_executable(bench_blur bench_blur.c)
target_link_libraries(bench_blur PRIVATE cvcl cvcl_compile_options)

# Release flags for benchmarking
if(NOT MSVC)
    target_compile_options(bench_blur PRIVATE -O3 -march=native)
endif()
target_compile_definitions(bench_blur PRIVATE _POSIX_C_SOURCE=199309L)
