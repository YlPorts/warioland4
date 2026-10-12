cmake_minimum_required(VERSION 3.22.1)
project(wl4_native LANGUAGES C CXX)

add_library(wl4_native SHARED
    native_bridge.cpp
    rom_assets.cpp
    gba_video.cpp
    frame_clock.cpp
    native_memory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src/fixed_point.c
)
target_include_directories(wl4_native PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../include)
target_compile_features(wl4_native PRIVATE cxx_std_17)
target_compile_options(wl4_native PRIVATE -Wall -Wextra -Werror=return-type)
target_link_libraries(wl4_native GLESv2 log)
