kota_add_git_dependency(
    libuv
    GIT_REPOSITORY https://github.com/libuv/libuv.git
    GIT_TAG v1.52.0
    OPTIONS
        LIBUV_BUILD_TESTS OFF
        LIBUV_BUILD_BENCH OFF
        LIBUV_BUILD_SHARED OFF
        LIBUV_BUILD_STATIC ON
        UV_LINT_W4 OFF
        # We manage sanitizers at the top-level (KOTA_SANITIZE_*). libuv's internal
        # sanitizer flags can cause mismatched link flags and, on Windows+clang-cl,
        # tries to copy runtime DLLs from the compiler dir which doesn't work for
        # some toolchain layouts (e.g. pixi/conda).
        ASAN OFF
        MSAN OFF
        TSAN OFF
        UBSAN OFF
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

kota_add_git_dependency(
    efsw
    GIT_REPOSITORY https://github.com/SpartanJ/efsw.git
    GIT_TAG 1.5.1
    OPTIONS
        BUILD_SHARED_LIBS OFF
        BUILD_STATIC_LIBS ON
        BUILD_TEST_APP OFF
        EFSW_INSTALL OFF
)

add_library(kota_async STATIC)
add_library(kota::async ALIAS kota_async)

target_sources(kota_async PRIVATE
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
    "${CMAKE_CURRENT_SOURCE_DIR}/io/system.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/udp.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/watcher.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/directory_watcher.cpp"
)

target_include_directories(kota_async PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(kota_async PUBLIC
    libuv::libuv
)

target_link_libraries(kota_async PRIVATE
    efsw-static
)

kota_apply_project_options(kota_async)
