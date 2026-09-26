# Tests are grouped by module, each directory mirroring include/kota/<module>/,
# and inside a module by level (see the test-style skill):
#   unit/     touches only the process's memory, event loop and timers
#             included; built into unit_tests
#   system/   touches the operating system: files, processes, sockets, pipes,
#             threads; built into system_tests
#   harness/  helpers for the module's tests and the modules above it
# Modules are added in dependency order.

set(kota_test_binaries unit_tests system_tests)

foreach(binary IN LISTS kota_test_binaries)
    add_executable(${binary} main.cpp)
    target_include_directories(${binary} PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(${binary} PRIVATE
        kota::zest
        cpptrace::cpptrace
    )
    kota_apply_project_options(${binary})
    if(MSVC)
        target_compile_options(${binary} PRIVATE /bigobj)
    endif()
endforeach()

# Adds the calling module's unit/ and system/ tests to their binaries, linked
# against the module's libraries.
function(kota_add_module_tests)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "" "LIBS")
    foreach(level IN ITEMS unit system)
        file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${level}/*.cpp")
        target_sources(${level}_tests PRIVATE ${sources})
        target_link_libraries(${level}_tests PRIVATE ${arg_LIBS})
    endforeach()
endfunction()

add_subdirectory(support)
add_subdirectory(meta)
add_subdirectory(codec)
if(KOTA_ENABLE_DECO)
    add_subdirectory(deco)
endif()
add_subdirectory(async)
if(KOTA_CODEC_ENABLE_SIMDJSON)
    add_subdirectory(ipc)
endif()
if(KOTA_ENABLE_HTTP)
    add_subdirectory(http)
endif()
add_subdirectory(zest)

# The test executables, and zest's runner fixture, sit together in the build
# root so that they find the same runtime DLLs.
set_target_properties(${kota_test_binaries} zest_runner_fixture PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
)

if(KOTA_SANITIZE_ADDRESS AND MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # clang-cl ASan uses a runtime DLL; copy it next to each test executable so
    # that any of them runs, from ctest or alone, without tweaking PATH.
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
            foreach(binary IN LISTS kota_test_binaries ITEMS zest_runner_fixture)
                add_custom_command(TARGET ${binary} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${kota_asan_runtime_path}"
                        "$<TARGET_FILE_DIR:${binary}>"
                    VERBATIM
                )
            endforeach()
        else()
            message(WARNING
                "KOTA_SANITIZE_ADDRESS=ON, but ${kota_asan_runtime_name} was not found. "
                "Tried: ${kota_asan_runtime_search_paths}"
            )
        endif()
    endforeach()
endif()

# zest runs tests on worker processes it drives through kota::async, compares
# with meta, prints with the debug codec, matches filters with support's glob
# patterns and parses its options with deco, so the checks are staged and a
# failed stage keeps the later ones from running:
#   1. zest_bootstrap_*: the tests of what zest itself relies on run in this
#      process, without the pool.
#   2. zest_runner: the pool itself, end to end, against tests that crash,
#      hang or exit early.
#   3. unit_tests, system_tests: everything, through the pool.
add_test(NAME zest_bootstrap_unit
    COMMAND unit_tests
        --no-isolation
        "--test-filter={support_functional,support_glob_pattern,meta_compare,meta_name,codec_debug_*,deco_*,async_runtime_task,async_runtime_when_*,async_io_watcher}.*"
        "--snapshot-dir=tests/snapshots"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
add_test(NAME zest_bootstrap_system
    COMMAND system_tests
        --no-isolation
        "--test-filter={async_io_fs,async_io_process,async_io_stream_pipe,async_io_system}.*"
        "--snapshot-dir=tests/snapshots"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
set_tests_properties(zest_bootstrap_unit zest_bootstrap_system PROPERTIES
    FIXTURES_SETUP zest_async
    LABELS bootstrap
)

add_test(NAME zest_runner
    COMMAND "${CMAKE_COMMAND}"
        "-DFIXTURE=$<TARGET_FILE:zest_runner_fixture>"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/zest_runner"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/zest/integration/check_runner.cmake"
)
set_tests_properties(zest_runner PROPERTIES
    FIXTURES_REQUIRED zest_async
    FIXTURES_SETUP zest_pool
    LABELS bootstrap
)

foreach(binary IN LISTS kota_test_binaries)
    string(REGEX REPLACE "_tests$" "" level "${binary}")
    add_test(NAME ${binary}
        COMMAND ${binary} "--snapshot-dir=tests/snapshots"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    )
    set_tests_properties(${binary} PROPERTIES
        FIXTURES_REQUIRED "zest_async;zest_pool"
        LABELS ${level}
    )
endforeach()
