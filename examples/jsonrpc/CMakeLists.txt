add_executable(eventide_example_jsonrpc_stdio_server
    "${CMAKE_CURRENT_SOURCE_DIR}/stdio_server.cpp"
)

target_include_directories(eventide_example_jsonrpc_stdio_server PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(eventide_example_jsonrpc_stdio_server PRIVATE
    eventide::jsonrpc
)

add_executable(eventide_example_jsonrpc_scripted_roundtrip
    "${CMAKE_CURRENT_SOURCE_DIR}/scripted_roundtrip.cpp"
)

target_include_directories(eventide_example_jsonrpc_scripted_roundtrip PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(eventide_example_jsonrpc_scripted_roundtrip PRIVATE
    eventide::jsonrpc
)

add_executable(spawn_worker
    "${CMAKE_CURRENT_SOURCE_DIR}/spawn_worker.cpp"
)

target_include_directories(spawn_worker PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(spawn_worker PRIVATE
    eventide::jsonrpc
)
