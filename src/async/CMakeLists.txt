eventide_add_git_dependency(
    libuv
    GIT_REPOSITORY https://github.com/libuv/libuv.git
    GIT_TAG v1.52.0
    OPTIONS
        LIBUV_BUILD_TESTS OFF
        LIBUV_BUILD_BENCH OFF
        LIBUV_BUILD_SHARED OFF
        LIBUV_BUILD_STATIC ON
        UV_LINT_W4 OFF
)

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
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/debug.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/frame.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/sync.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vocab/error.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vocab/ringbuffer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/acceptor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/console.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/fs.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/loop.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/process.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/request.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/stream.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/udp.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/watcher.cpp"
)

target_include_directories(eventide_async PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(eventide_async PUBLIC
    libuv::libuv
)

eventide_apply_project_options(eventide_async)
