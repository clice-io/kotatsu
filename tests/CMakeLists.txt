add_executable(unit_tests)
# zest's own runner checked end to end (see zest_runner below); it sits next to
# unit_tests so that it finds the same runtime DLLs.
add_executable(zest_runner_fixture "${PROJECT_SOURCE_DIR}/tests/zest/runner_fixture.cpp")
target_link_libraries(zest_runner_fixture PRIVATE kota::zest)
kota_apply_project_options(zest_runner_fixture)
set_target_properties(unit_tests zest_runner_fixture PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
)

file(GLOB_RECURSE TEST_SOURCES RELATIVE "${PROJECT_SOURCE_DIR}" CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/tests/unit/*.cpp"
    "${PROJECT_SOURCE_DIR}/tests/unit/*.cc"
)

set(TEST_BASE_SOURCES ${TEST_SOURCES})
list(FILTER TEST_BASE_SOURCES INCLUDE REGEX "^tests/unit/(main\\.cpp|support/.*\\.cpp|meta/.*\\.cpp|zest/.*\\.cpp|async/.*\\.cpp)$")
list(TRANSFORM TEST_BASE_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
target_sources(unit_tests PRIVATE ${TEST_BASE_SOURCES})

if(KOTA_ENABLE_DECO)
    set(TEST_DECO_SOURCES ${TEST_SOURCES})
    list(FILTER TEST_DECO_SOURCES INCLUDE REGEX "^tests/unit/deco/.*\\.(cpp|cc)$")
    list(TRANSFORM TEST_DECO_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    target_sources(unit_tests PRIVATE
        ${TEST_DECO_SOURCES}
    )
else()
    message(STATUS "KOTA_ENABLE_DECO=OFF: skipping tests/unit/deco/*")
endif()

if(KOTA_CODEC_ENABLE_SIMDJSON)
    set(TEST_CODEC_SIMDJSON_SOURCES ${TEST_SOURCES})
    list(FILTER TEST_CODEC_SIMDJSON_SOURCES INCLUDE REGEX "^tests/unit/codec/json/.*\\.cpp$")
    list(TRANSFORM TEST_CODEC_SIMDJSON_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    target_sources(unit_tests PRIVATE
        ${TEST_CODEC_SIMDJSON_SOURCES}
    )

    set(TEST_CODEC_CONTENT_SOURCES ${TEST_SOURCES})
    list(FILTER TEST_CODEC_CONTENT_SOURCES INCLUDE REGEX "^tests/unit/codec/dyn/.*\\.cpp$")
    list(TRANSFORM TEST_CODEC_CONTENT_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    target_sources(unit_tests PRIVATE
        ${TEST_CODEC_CONTENT_SOURCES}
    )

    # Lets always-built suites opt into cases that need the JSON codec, such as
    # zest's EXPECT_SNAPSHOT_JSON tests.
    target_compile_definitions(unit_tests PRIVATE KOTA_TEST_HAS_JSON=1)
else()
    message(STATUS "KOTA_CODEC_ENABLE_SIMDJSON=OFF: skipping tests/unit/codec/{json,content}/*")
endif()

if(KOTA_CODEC_ENABLE_FLATBUFFERS)
    set(TEST_CODEC_FLATBUFFERS_SOURCES ${TEST_SOURCES})
    list(FILTER TEST_CODEC_FLATBUFFERS_SOURCES INCLUDE REGEX "^tests/unit/codec/fbs/.*\\.cpp$")
    list(TRANSFORM TEST_CODEC_FLATBUFFERS_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    target_sources(unit_tests PRIVATE
        ${TEST_CODEC_FLATBUFFERS_SOURCES}
    )
else()
    message(STATUS "KOTA_CODEC_ENABLE_FLATBUFFERS=OFF: skipping tests/unit/codec/fbs/*")
endif()

if(KOTA_CODEC_ENABLE_TOML)
    set(TEST_CODEC_TOML_SOURCES ${TEST_SOURCES})
    list(FILTER TEST_CODEC_TOML_SOURCES INCLUDE REGEX "^tests/unit/codec/toml/.*\\.cpp$")
    list(TRANSFORM TEST_CODEC_TOML_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    target_sources(unit_tests PRIVATE
        ${TEST_CODEC_TOML_SOURCES}
    )
else()
    message(STATUS "KOTA_CODEC_ENABLE_TOML=OFF: skipping tests/unit/codec/toml/*")
endif()

set(TEST_CODEC_BINCODE_SOURCES ${TEST_SOURCES})
list(FILTER TEST_CODEC_BINCODE_SOURCES INCLUDE REGEX "^tests/unit/codec/bincode/.*\\.cpp$")
list(TRANSFORM TEST_CODEC_BINCODE_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
target_sources(unit_tests PRIVATE
    ${TEST_CODEC_BINCODE_SOURCES}
)

set(TEST_CODEC_DEBUG_SOURCES ${TEST_SOURCES})
list(FILTER TEST_CODEC_DEBUG_SOURCES INCLUDE REGEX "^tests/unit/codec/debug/.*\\.cpp$")
list(TRANSFORM TEST_CODEC_DEBUG_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
target_sources(unit_tests PRIVATE
    ${TEST_CODEC_DEBUG_SOURCES}
)

if(KOTA_ENABLE_HTTP)
    set(TEST_HTTP_SOURCES ${TEST_SOURCES})
    list(FILTER TEST_HTTP_SOURCES INCLUDE REGEX "^tests/unit/http/.*\\.cpp$")
    list(TRANSFORM TEST_HTTP_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    target_sources(unit_tests PRIVATE
        ${TEST_HTTP_SOURCES}
    )
    target_link_libraries(unit_tests PRIVATE
        kota::http
    )
else()
    message(STATUS "KOTA_ENABLE_HTTP=OFF: skipping tests/unit/http/*")
endif()

if(KOTA_CODEC_ENABLE_SIMDJSON)
    set(TEST_IPC_SOURCES ${TEST_SOURCES})
    list(FILTER TEST_IPC_SOURCES INCLUDE REGEX "^tests/unit/ipc/.*\\.cpp$")
    list(TRANSFORM TEST_IPC_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    target_sources(unit_tests PRIVATE
        ${TEST_IPC_SOURCES}
    )
    target_link_libraries(unit_tests PRIVATE
        kota::ipc
        kota::ipc::lsp
    )
else()
    message(STATUS "KOTA_CODEC_ENABLE_SIMDJSON=OFF: skipping tests/unit/ipc/*")
endif()

target_include_directories(unit_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/examples/build_system"
    "${PROJECT_SOURCE_DIR}/tests"
)

target_link_libraries(unit_tests PRIVATE
    kota::async
    kota::codec
    kota::support
    kota::zest
    cpptrace::cpptrace
)

if(KOTA_ENABLE_DECO)
    target_link_libraries(unit_tests PRIVATE
        kota::deco
    )
endif()

kota_apply_project_options(unit_tests)

if(MSVC)
    target_compile_options(unit_tests PRIVATE /bigobj)
endif()

if(KOTA_SANITIZE_ADDRESS AND MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # clang-cl ASan uses a runtime DLL; copy it next to the test executable so `ctest` can run
    # without requiring CI to tweak PATH.
    get_filename_component(kota_clang_compiler_dir "${CMAKE_C_COMPILER}" DIRECTORY)
    set(kota_asan_runtime_search_paths
        "${kota_clang_compiler_dir}"
        "${kota_asan_lib_path}"
    )

    foreach(kota_asan_runtime_name IN ITEMS
        clang_rt.asan_dynamic-x86_64.dll
        clang_rt.asan_dbg_dynamic-x86_64.dll
    )
        set(kota_asan_runtime_path "")
        foreach(kota_asan_runtime_dir IN LISTS kota_asan_runtime_search_paths)
            if(kota_asan_runtime_dir AND EXISTS "${kota_asan_runtime_dir}/${kota_asan_runtime_name}")
                set(kota_asan_runtime_path "${kota_asan_runtime_dir}/${kota_asan_runtime_name}")
                break()
            endif()
        endforeach()

        if(kota_asan_runtime_path)
            add_custom_command(TARGET unit_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${kota_asan_runtime_path}"
                    "$<TARGET_FILE_DIR:unit_tests>"
                VERBATIM
            )
        else()
            message(WARNING
                "KOTA_SANITIZE_ADDRESS=ON, but ${kota_asan_runtime_name} was not found. "
                "Tried: ${kota_asan_runtime_search_paths}"
            )
        endif()
    endforeach()
endif()
add_test(NAME unit_tests COMMAND unit_tests
    "--snapshot-dir=tests/snapshots"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)

# zest runs tests on worker processes it drives through kota::async, so the
# checks are staged and a failed stage keeps the later ones from running:
#   1. zest_bootstrap: the async tests covering what the worker pool uses run
#      in this process, without the pool.
#   2. zest_runner: the pool itself, end to end, against tests that crash,
#      hang or exit early.
#   3. unit_tests: everything, through the pool.
add_test(NAME zest_bootstrap
    COMMAND unit_tests
        --no-isolation
        "--test-filter={task,when_all,when_any,when_cancel,process_io,pipe,watcher_io,system_info}.*"
        "--snapshot-dir=tests/snapshots"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
set_tests_properties(zest_bootstrap PROPERTIES FIXTURES_SETUP zest_async)

add_test(NAME zest_runner
    COMMAND "${CMAKE_COMMAND}"
        "-DFIXTURE=$<TARGET_FILE:zest_runner_fixture>"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/zest_runner"
        -P "${PROJECT_SOURCE_DIR}/tests/zest/check_runner.cmake"
)
set_tests_properties(zest_runner PROPERTIES
    FIXTURES_REQUIRED zest_async
    FIXTURES_SETUP zest_pool
)
set_tests_properties(unit_tests PROPERTIES FIXTURES_REQUIRED "zest_async;zest_pool")
