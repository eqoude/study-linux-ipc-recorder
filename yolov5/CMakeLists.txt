cmake_minimum_required(VERSION 3.15)

set(RKNN_YOLO_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(RKNN_YOLO_THIRD_PARTY_DIR ${RKNN_YOLO_DIR}/third_party)
set(RKNN_YOLO_RKNNRT ${RKNN_YOLO_THIRD_PARTY_DIR}/rknpu2/Linux/aarch64/librknnrt.so)
set(RKNN_YOLO_RGA ${RKNN_YOLO_THIRD_PARTY_DIR}/rga/Linux/aarch64/librga.a)
set(RKNN_YOLO_TURBOJPEG ${RKNN_YOLO_THIRD_PARTY_DIR}/jpeg_turbo/Linux/aarch64/libturbojpeg.a)

add_library(rknn_yolo STATIC
    wrapper/rknn_yolo_wrapper.cpp
    wrapper/rknn_yolo_media_adapter.cpp
    inference/rknpu2/yolov5.cc
    inference/postprocess.cc
    utils/image_utils.c
    utils/file_utils.c
    utils/image_drawing.c
)

set_target_properties(rknn_yolo PROPERTIES
    OUTPUT_NAME rknn_yolo
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(rknn_yolo PUBLIC
    ${RKNN_YOLO_DIR}/wrapper
    ${CMAKE_SOURCE_DIR}/core
    ${RKNN_YOLO_DIR}/inference
    ${RKNN_YOLO_DIR}/inference/rknpu2
    ${RKNN_YOLO_DIR}/utils
    ${RKNN_YOLO_THIRD_PARTY_DIR}/rknpu2/include
    ${RKNN_YOLO_THIRD_PARTY_DIR}/rga/include
    ${RKNN_YOLO_THIRD_PARTY_DIR}/stb_image
    ${RKNN_YOLO_THIRD_PARTY_DIR}/jpeg_turbo/include
)

target_compile_definitions(rknn_yolo PRIVATE
    LIBRGA_IM2D_HANDLE
)

target_link_libraries(rknn_yolo PUBLIC
    ${RKNN_YOLO_RKNNRT}
    ${RKNN_YOLO_RGA}
    ${RKNN_YOLO_TURBOJPEG}
    Threads::Threads
    dl
)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$" OR
   CMAKE_CXX_COMPILER MATCHES "aarch64")
    add_executable(test_yolo_infer
        test/test_yolo_infer.cpp
    )

    set_target_properties(test_yolo_infer PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )

    target_link_libraries(test_yolo_infer PRIVATE
        rknn_yolo
    )

    add_executable(test_media_adapter
        test/test_media_adapter.cpp
    )

    set_target_properties(test_media_adapter PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )

    target_link_libraries(test_media_adapter PRIVATE
        rknn_yolo
    )
else()
    message(WARNING
        "Skipping RKNN test executables: RKNN runtime is aarch64, "
        "but current target processor is ${CMAKE_SYSTEM_PROCESSOR}. "
        "Build on RK3588 or use an aarch64 toolchain to generate the test executable."
    )
endif()
