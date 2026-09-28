# Example executables go to ${CMAKE_BINARY_DIR}/examples, but for those the
# integration tests run, which tests/CMakeLists.txt puts in the build root
# beside the test programs.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples")

if(KOTA_ENABLE_ASYNC)
    add_executable(async_basics async_basics/async_basics.cpp)
    target_include_directories(async_basics PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(async_basics PRIVATE kota::async)

    add_executable(build_system build_system/build_system.cpp)
    target_include_directories(build_system PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/build_system"
    )
    target_link_libraries(build_system PRIVATE kota::async)

    add_executable(dump_dot dump_dot/dump_dot.cpp)
    target_include_directories(dump_dot PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(dump_dot PRIVATE kota::async)
else()
    message(STATUS "KOTA_ENABLE_ASYNC=OFF: skipping async examples")
endif()

if(KOTA_ENABLE_ASYNC AND KOTA_CODEC_ENABLE_SIMDJSON)
    add_executable(jsonrpc_server ipc/stdio_server.cpp)
    target_include_directories(jsonrpc_server PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(jsonrpc_server PRIVATE kota::ipc)

    add_executable(jsonrpc_roundtrip ipc/scripted_roundtrip.cpp)
    target_include_directories(jsonrpc_roundtrip PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(jsonrpc_roundtrip PRIVATE kota::ipc)

    add_executable(spawn_worker ipc/spawn_worker.cpp)
    target_include_directories(spawn_worker PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(spawn_worker PRIVATE kota::ipc)
else()
    message(STATUS "KOTA_ENABLE_ASYNC=OFF or KOTA_CODEC_ENABLE_SIMDJSON=OFF: skipping ipc examples")
endif()

if(KOTA_ENABLE_HTTP)
    add_executable(github_avatar_download http/github_avatar_download.cpp)
    target_include_directories(github_avatar_download PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(github_avatar_download PRIVATE kota::http)

    add_executable(http_showcase http/http_showcase.cpp)
    target_include_directories(http_showcase PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(http_showcase PRIVATE kota::http)

    add_executable(httpbin_cookie_flow http/httpbin_cookie_flow.cpp)
    target_include_directories(httpbin_cookie_flow PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(httpbin_cookie_flow PRIVATE kota::http)

    add_executable(webdav_list http/webdav_list.cpp)
    target_include_directories(webdav_list PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(webdav_list PRIVATE kota::http)
else()
    message(STATUS "KOTA_ENABLE_HTTP=OFF: skipping http examples")
endif()

if(KOTA_ENABLE_HTTP AND KOTA_CODEC_ENABLE_SIMDJSON)
    add_executable(openai_responses http/openai_responses.cpp)
    target_include_directories(openai_responses PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(openai_responses PRIVATE kota::http)
else()
    message(STATUS "KOTA_ENABLE_HTTP=OFF or KOTA_CODEC_ENABLE_SIMDJSON=OFF: skipping openai_responses example")
endif()
