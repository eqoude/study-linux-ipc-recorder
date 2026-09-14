add_executable(ipc_recorder
    main.c
    app_config.c
    app_pipeline.c
)

target_include_directories(ipc_recorder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ipc_recorder PRIVATE
    modules
    core
    capture
    converter
    encoder
    frame_processor
    muxer
    sink
    Threads::Threads
    ipc_compile_options
)

if(ENABLE_VIEWER)
    target_compile_definitions(ipc_recorder PRIVATE ENABLE_VIEWER)
    target_link_libraries(ipc_recorder PRIVATE viewer)
endif()
