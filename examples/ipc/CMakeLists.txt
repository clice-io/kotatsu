add_executable(kota_example_ipc_stdio_server
    "${CMAKE_CURRENT_SOURCE_DIR}/stdio_server.cpp"
)

target_include_directories(kota_example_ipc_stdio_server PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(kota_example_ipc_stdio_server PRIVATE
    kota::ipc
)

add_executable(kota_example_ipc_scripted_roundtrip
    "${CMAKE_CURRENT_SOURCE_DIR}/scripted_roundtrip.cpp"
)

target_include_directories(kota_example_ipc_scripted_roundtrip PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(kota_example_ipc_scripted_roundtrip PRIVATE
    kota::ipc
)

add_executable(spawn_worker
    "${CMAKE_CURRENT_SOURCE_DIR}/spawn_worker.cpp"
)

target_include_directories(spawn_worker PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(spawn_worker PRIVATE
    kota::ipc
)
