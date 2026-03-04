FetchContent_Declare(
    libuv
    GIT_REPOSITORY https://github.com/libuv/libuv.git
    GIT_TAG v1.52.0
    GIT_SHALLOW TRUE
)

# Configure libuv via FetchContent so no system install is required.
if(NOT WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(ASAN ON CACHE BOOL "Enable AddressSanitizer for libuv" FORCE)
endif()
set(LIBUV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LIBUV_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(LIBUV_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(LIBUV_BUILD_STATIC ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libuv)

if(WIN32)
    target_compile_definitions(uv_a PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(NOT MSVC AND TARGET uv_a)
    target_compile_options(uv_a PRIVATE
        "-Wno-unused-function"
        "-Wno-unused-variable"
        "-Wno-unused-but-set-variable"
        "-Wno-deprecated-declarations"
        "-Wno-missing-braces"
    )
endif()

add_library(eventide_async STATIC)
add_library(eventide::async ALIAS eventide_async)

target_sources(eventide_async PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/error.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/frame.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/fs.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/loop.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/process.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/request.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ringbuffer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/acceptor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/console.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/stream.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/udp.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/watcher.cpp"
)

target_include_directories(eventide_async PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(eventide_async PUBLIC
    libuv::libuv
)
