cmake_minimum_required(VERSION 3.15)

project(
    ipc_recorder
    VERSION 1.0
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

option(ENABLE_VIEWER "Enable SDL viewer" OFF)
option(ENABLE_RKNN "Enable RKNN AI" OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavcodec
    libavformat
    libavutil
    libswscale
)

add_library(ipc_compile_options INTERFACE)
target_compile_options(ipc_compile_options INTERFACE
    -Wall
    -Wextra
    -g
    -O0
)

add_subdirectory(core)
add_subdirectory(capture)
add_subdirectory(converter)
add_subdirectory(encoder)
add_subdirectory(frame_processor)
add_subdirectory(muxer)
add_subdirectory(sink)

if(ENABLE_VIEWER)
    add_subdirectory(viewer)
endif()

if(ENABLE_RKNN)
    enable_language(CXX)
    add_subdirectory(yolov5)
endif()

add_subdirectory(modules)
add_subdirectory(app)
