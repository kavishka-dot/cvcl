cmake_minimum_required(VERSION 3.16)

add_executable(demo_pipeline demo_pipeline.c)
target_link_libraries(demo_pipeline PRIVATE cvcl cvcl_compile_options)

# Tutorial programs
set(TUTORIAL_SOURCES
    tutorial/01_hello_image.c
    tutorial/02_load_and_inspect.c
    tutorial/03_channels.c
    tutorial/04_blur.c
    tutorial/05_edges.c
    tutorial/06_morphology.c
    tutorial/07_threshold.c
    tutorial/08_custom_allocator.c
)

foreach(src ${TUTORIAL_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE cvcl cvcl_compile_options)
    if(NOT WIN32)
        target_link_libraries(${name} PRIVATE m)
    endif()
endforeach()
