kotatsu_add_git_dependency(
    libuv
    GIT_REPOSITORY https://github.com/libuv/libuv.git
    GIT_TAG v1.52.0
    OPTIONS
        LIBUV_BUILD_TESTS OFF
        LIBUV_BUILD_BENCH OFF
        LIBUV_BUILD_SHARED OFF
        LIBUV_BUILD_STATIC ON
        UV_LINT_W4 OFF
        # We manage sanitizers at the top-level (KOTATSU_SANITIZE_*). libuv's internal
        # sanitizer flags can cause mismatched link flags and, on Windows+clang-cl,
        # tries to copy runtime DLLs from the compiler dir which doesn't work for
        # some toolchain layouts (e.g. pixi/conda).
        ASAN OFF
        MSAN OFF
        TSAN OFF
        UBSAN OFF
)

add_library(kotatsu_async STATIC)
add_library(kotatsu::async ALIAS kotatsu_async)

target_sources(kotatsu_async PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/cancellation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/debug.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/node.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/sync.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vocab/error.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/acceptor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/console.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/fs.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/loop.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/process.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/request.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/ring_buffer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/stream.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/udp.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/watcher.cpp"
)

target_include_directories(kotatsu_async PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(kotatsu_async PUBLIC
    kotatsu::fs
    libuv::libuv
)

# Exit status names and the environment of child processes.
target_link_libraries(kotatsu_async PRIVATE
    kotatsu::sys
)

kotatsu_apply_project_options(kotatsu_async)
