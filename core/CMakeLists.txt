add_library(core STATIC
    ipc_error.c
    media_packet.c
    thread_queue.c
    signal_handler.c
)

target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(core PRIVATE
    ipc_compile_options
)
