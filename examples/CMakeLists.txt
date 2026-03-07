# All example executables are output directly into ${CMAKE_BINARY_DIR}/examples.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

if(EVENTIDE_ENABLE_ASYNC)
    add_executable(async_basics async_basics/async_basics.cpp)
    target_include_directories(async_basics PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(async_basics PRIVATE eventide::async)

    add_executable(build_system build_system/build_system.cpp)
    target_include_directories(build_system PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/build_system"
    )
    target_link_libraries(build_system PRIVATE eventide::async)

    add_executable(dump_dot dump_dot/dump_dot.cpp)
    target_include_directories(dump_dot PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(dump_dot PRIVATE eventide::async)
else()
    message(STATUS "EVENTIDE_ENABLE_ASYNC=OFF: skipping async examples")
endif()

if(EVENTIDE_ENABLE_ASYNC AND EVENTIDE_SERDE_ENABLE_SIMDJSON)
    add_executable(jsonrpc_server ipc/stdio_server.cpp)
    target_include_directories(jsonrpc_server PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(jsonrpc_server PRIVATE eventide::ipc)

    add_executable(jsonrpc_roundtrip ipc/scripted_roundtrip.cpp)
    target_include_directories(jsonrpc_roundtrip PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(jsonrpc_roundtrip PRIVATE eventide::ipc)

    add_executable(spawn_worker ipc/spawn_worker.cpp)
    target_include_directories(spawn_worker PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(spawn_worker PRIVATE eventide::ipc)
else()
    message(STATUS "EVENTIDE_ENABLE_ASYNC=OFF or EVENTIDE_SERDE_ENABLE_SIMDJSON=OFF: skipping ipc examples")
endif()
