cmake_minimum_required(VERSION 3.16)

set(TEST_SOURCES
    test_image.c
    test_io.c
    test_transform.c
    test_filter.c
    test_morph.c
    test_correctness.c
    test_edge_cases.c
    test_memory.c
    test_perf.c
    test_resource_constraints.c
    test_numerical.c
)

foreach(src ${TEST_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE cvcl cvcl_compile_options)
    if(NOT WIN32)
        target_link_libraries(${name} PRIVATE m)
    endif()
    if(name STREQUAL "test_perf" AND NOT MSVC)
        target_compile_definitions(${name} PRIVATE _POSIX_C_SOURCE=199309L)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endforeach()
