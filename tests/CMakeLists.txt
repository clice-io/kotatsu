# Tests are grouped by module, each directory mirroring include/kota/<module>/,
# and inside a module by level (see the test-style skill):
#   unit/     touches only the process's memory, event loop and timers
#             included; built into unit_tests
#   system/   touches the operating system: files, processes, sockets, pipes,
#             threads; built into system_tests
#   harness/  helpers for the module's tests and the modules above it
#   integration/
#             programs talking over real protocols: TypeScript tests on
#             node's test runner, and the drivers they spawn
# Modules are added in dependency order; examples/ holds the tests of the
# examples, by module, last. harness/ here holds what every module's
# integration tests share.

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

# The integration tests run on node, which runs TypeScript without a build
# step from 22.18 on. Without it they are left out, which only a build of all
# tests refuses.
find_program(KOTA_NODE node)
set(kota_node_problem "")
if(NOT KOTA_NODE)
    set(kota_node_problem "node was not found")
else()
    execute_process(COMMAND "${KOTA_NODE}" --version
        RESULT_VARIABLE kota_node_status
        OUTPUT_VARIABLE kota_node_version
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    string(REGEX REPLACE "^v" "" kota_node_version "${kota_node_version}")
    if(NOT kota_node_status EQUAL 0 OR NOT kota_node_version)
        set(kota_node_problem "${KOTA_NODE} --version failed")
    elseif(kota_node_version VERSION_LESS 22.18)
        set(kota_node_problem "${KOTA_NODE} is node ${kota_node_version}, older than 22.18")
    endif()
endif()
if(kota_node_problem)
    if(KOTA_BUILD_ALL_TESTS)
        message(FATAL_ERROR
            "The integration tests need node 22.18 or newer, but ${kota_node_problem}; "
            "configure through pixi (pixi run build)."
        )
    endif()
    message(STATUS "Leaving out the integration tests: ${kota_node_problem}.")
endif()

# A plain Debug build, unoptimized and without sanitizers, compiles the
# largest translation units fastest and in the least memory. A multi-config
# generator picks the configuration at build time, which configure cannot
# see, so it is never taken for one.
get_property(kota_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
string(TOLOWER "${CMAKE_BUILD_TYPE}" kota_build_type)
if(NOT kota_multi_config
    AND kota_build_type STREQUAL "debug"
    AND NOT (KOTA_SANITIZE_ADDRESS OR KOTA_SANITIZE_MEMORY OR KOTA_SANITIZE_THREAD OR KOTA_SANITIZE_UNDEFINED))
    set(kota_plain_debug ON)
else()
    set(kota_plain_debug OFF)
endif()

# Adds the calling module's integration tests, integration/*.test.ts, as one
# ctest test that node runs from the repo root; their npm packages come from
# the root package.json (`pixi run npm-ci`). Each integration/drivers/<name>.cpp
# builds the driver <name>, linked against the module's libraries LIBS; the
# tests find its path in the environment variable KOTA_<NAME>, and so the path
# of each program PROGRAMS names, targets built elsewhere. DEBUG_ONLY names
# files under integration/, tests and drivers, that only a plain Debug build
# builds and runs; elsewhere such a driver is left out of the default build.
function(kota_add_integration_tests)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "" "LIBS;PROGRAMS;DEBUG_ONLY")
    if(kota_node_problem)
        return()
    endif()

    set(left_out "")
    foreach(file IN LISTS arg_DEBUG_ONLY)
        set(path "${CMAKE_CURRENT_SOURCE_DIR}/integration/${file}")
        if(NOT EXISTS "${path}")
            message(FATAL_ERROR "DEBUG_ONLY names ${path}, which does not exist.")
        endif()
        if(NOT kota_plain_debug)
            list(APPEND left_out "${path}")
        endif()
    endforeach()

    set(programs "")
    file(GLOB drivers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/integration/drivers/*.cpp")
    foreach(source IN LISTS drivers)
        get_filename_component(driver "${source}" NAME_WE)
        if(source IN_LIST left_out)
            add_executable(${driver} EXCLUDE_FROM_ALL "${source}")
        else()
            add_executable(${driver} "${source}")
            list(APPEND programs ${driver})
            set_property(GLOBAL APPEND PROPERTY kota_default_drivers ${driver})
        endif()
        target_include_directories(${driver} PRIVATE "${PROJECT_SOURCE_DIR}/tests")
        target_link_libraries(${driver} PRIVATE ${arg_LIBS})
        kota_apply_project_options(${driver})
        if(MSVC)
            target_compile_options(${driver} PRIVATE /bigobj)
        endif()
        set_property(GLOBAL APPEND PROPERTY kota_integration_drivers ${driver})
    endforeach()
    set_property(GLOBAL APPEND PROPERTY kota_integration_programs ${arg_PROGRAMS})

    set(environment "")
    foreach(program IN LISTS programs arg_PROGRAMS)
        string(TOUPPER "KOTA_${program}" variable)
        list(APPEND environment "${variable}=$<TARGET_FILE:${program}>")
    endforeach()

    file(RELATIVE_PATH module "${PROJECT_SOURCE_DIR}/tests" "${CMAKE_CURRENT_SOURCE_DIR}")
    string(REPLACE "/" "_" name "${module}_integration")
    file(GLOB tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/integration/*.test.ts")
    if(NOT tests)
        message(FATAL_ERROR "${CMAKE_CURRENT_SOURCE_DIR}/integration has no *.test.ts.")
    endif()
    if(left_out)
        list(REMOVE_ITEM tests ${left_out})
    endif()
    # node --test given no file would run every *.test.ts it finds.
    if(NOT tests)
        return()
    endif()
    # A test that hangs fails at --test-timeout, its drivers killed; TIMEOUT
    # only catches the runner itself hanging.
    add_test(NAME ${name}
        COMMAND "${KOTA_NODE}"
            --import ./tests/check_npm_packages.ts
            --test
            --test-reporter=spec
            --test-timeout=20000
            ${tests}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    )
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "${environment}"
        LABELS integration
        TIMEOUT 600
    )
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
if(KOTA_ENABLE_EXAMPLES)
    add_subdirectory(examples)
endif()

# The test executables, zest's runner fixture, the integration drivers and the
# programs the integration tests run sit together in the build root so that
# they find the same runtime DLLs.
get_property(kota_integration_drivers GLOBAL PROPERTY kota_integration_drivers)
get_property(kota_integration_programs GLOBAL PROPERTY kota_integration_programs)
set(kota_test_programs
    ${kota_test_binaries}
    zest_runner_fixture
    ${kota_integration_drivers}
    ${kota_integration_programs}
)
set_target_properties(${kota_test_programs} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
)

# ninja starts the ready compiles with the most steps still after them first,
# and breaks ties by their order in build.ninja, which puts every module's
# drivers after the test binaries' sources. So the LSP drivers, the heaviest
# translation units of the tests, started last and ran on for minutes after
# the rest had linked. A step after every driver starts them right after the
# libraries; it runs a command because ninja does not count phony steps.
get_property(kota_default_drivers GLOBAL PROPERTY kota_default_drivers)
if(kota_default_drivers)
    add_custom_command(OUTPUT kota_integration_drivers.stamp
        COMMAND "${CMAKE_COMMAND}" -E touch kota_integration_drivers.stamp
        DEPENDS ${kota_default_drivers}
    )
    add_custom_target(kota_integration_drivers ALL DEPENDS kota_integration_drivers.stamp)
endif()

if(KOTA_SANITIZE_ADDRESS AND MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # clang-cl ASan uses a runtime DLL; copy it into the build root before any
    # program the tests run builds, so that each runs without tweaking PATH.
    # The copy is a target of its own because a build event can only be
    # attached in the directory that defines its target, and the drivers and
    # programs are defined elsewhere.
    add_custom_target(kota_asan_runtime)
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
            add_custom_command(TARGET kota_asan_runtime POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${kota_asan_runtime_path}"
                    "${CMAKE_BINARY_DIR}"
                VERBATIM
            )
        else()
            message(WARNING
                "KOTA_SANITIZE_ADDRESS=ON, but ${kota_asan_runtime_name} was not found. "
                "Tried: ${kota_asan_runtime_search_paths}"
            )
        endif()
    endforeach()

    foreach(program IN LISTS kota_test_programs)
        add_dependencies(${program} kota_asan_runtime)
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
        "--test-filter={support_*,meta_compare,meta_name,codec_debug_*,deco_*,async_vocab_outcome,async_vocab_error,async_runtime_task,async_runtime_when_*,async_io_loop,async_io_watcher}.*"
        "--snapshot-dir=tests/snapshots"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
add_test(NAME zest_bootstrap_system
    COMMAND system_tests
        --no-isolation
        "--test-filter={async_io_fs,async_io_fs_sync,async_io_process,async_io_stream_pipe,async_io_system}.*"
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
