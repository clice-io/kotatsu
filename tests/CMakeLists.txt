add_executable(unit_tests)
set_target_properties(unit_tests PROPERTIES
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
list(FILTER TEST_BASE_SOURCES INCLUDE REGEX "^tests/unit/(main\\.cpp|support/.*\\.cpp|meta/.*\\.cpp|zest/.*\\.cpp)$")
list(TRANSFORM TEST_BASE_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
target_sources(unit_tests PRIVATE ${TEST_BASE_SOURCES})

if(KOTA_ENABLE_OPTION)
    set(TEST_OPTION_SOURCES ${TEST_SOURCES})
    list(FILTER TEST_OPTION_SOURCES INCLUDE REGEX "^tests/unit/option/.*\\.cpp$")
    list(TRANSFORM TEST_OPTION_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    target_sources(unit_tests PRIVATE
        ${TEST_OPTION_SOURCES}
    )
else()
    message(STATUS "KOTA_ENABLE_OPTION=OFF: skipping tests/unit/option/*")
endif()

if(KOTA_ENABLE_DECO AND KOTA_ENABLE_OPTION)
    set(TEST_DECO_SOURCES ${TEST_SOURCES})
    list(FILTER TEST_DECO_SOURCES INCLUDE REGEX "^tests/unit/deco/.*\\.(cpp|cc)$")
    list(TRANSFORM TEST_DECO_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    target_sources(unit_tests PRIVATE
        ${TEST_DECO_SOURCES}
    )
elseif(KOTA_ENABLE_DECO)
    message(STATUS "KOTA_ENABLE_OPTION=OFF: skipping tests/unit/deco/*")
else()
    message(STATUS "KOTA_ENABLE_DECO=OFF: skipping tests/unit/deco/*")
endif()

if(KOTA_ENABLE_ASYNC)
    set(TEST_ASYNC_SOURCES ${TEST_SOURCES})
    list(FILTER TEST_ASYNC_SOURCES INCLUDE REGEX "^tests/unit/async/.*\\.cpp$")
    list(TRANSFORM TEST_ASYNC_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    target_sources(unit_tests PRIVATE
        ${TEST_ASYNC_SOURCES}
    )
else()
    message(STATUS "KOTA_ENABLE_ASYNC=OFF: skipping tests/unit/async/*")
endif()

if(KOTA_CODEC_ENABLE_SIMDJSON)
    set(TEST_CODEC_SIMDJSON_SOURCES ${TEST_SOURCES})
    list(FILTER TEST_CODEC_SIMDJSON_SOURCES INCLUDE REGEX "^tests/unit/codec/json/simdjson_.*\\.cpp$")
    list(TRANSFORM TEST_CODEC_SIMDJSON_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    target_sources(unit_tests PRIVATE
        ${TEST_CODEC_SIMDJSON_SOURCES}
    )

    set(TEST_CODEC_CONTENT_SOURCES ${TEST_SOURCES})
    list(FILTER TEST_CODEC_CONTENT_SOURCES INCLUDE REGEX "^tests/unit/codec/content/.*\\.cpp$")
    list(TRANSFORM TEST_CODEC_CONTENT_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    target_sources(unit_tests PRIVATE
        ${TEST_CODEC_CONTENT_SOURCES}
    )
else()
    message(STATUS "KOTA_CODEC_ENABLE_SIMDJSON=OFF: skipping tests/unit/codec/{json,content}/*")
endif()

if(KOTA_CODEC_ENABLE_FLATBUFFERS)
    set(TEST_CODEC_FLATBUFFERS_SOURCES ${TEST_SOURCES})
    list(FILTER TEST_CODEC_FLATBUFFERS_SOURCES INCLUDE REGEX "^tests/unit/codec/flatbuffers/.*\\.cpp$")
    list(TRANSFORM TEST_CODEC_FLATBUFFERS_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    target_sources(unit_tests PRIVATE
        ${TEST_CODEC_FLATBUFFERS_SOURCES}
    )
else()
    message(STATUS "KOTA_CODEC_ENABLE_FLATBUFFERS=OFF: skipping tests/unit/codec/flatbuffers/*")
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

if(KOTA_ENABLE_ASYNC AND KOTA_CODEC_ENABLE_SIMDJSON)
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
    message(STATUS "KOTA_ENABLE_ASYNC=OFF or KOTA_CODEC_ENABLE_SIMDJSON=OFF: skipping tests/unit/ipc/*")
endif()

target_include_directories(unit_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/examples/build_system"
    "${PROJECT_SOURCE_DIR}/tests"
)

target_link_libraries(unit_tests PRIVATE
    kota::codec
    kota::zest
    cpptrace::cpptrace
)

if(KOTA_ENABLE_OPTION)
    target_link_libraries(unit_tests PRIVATE
        kota::option
    )
endif()

if(KOTA_ENABLE_DECO AND KOTA_ENABLE_OPTION)
    target_link_libraries(unit_tests PRIVATE
        kota::deco
    )
endif()

if(KOTA_ENABLE_ASYNC)
    target_link_libraries(unit_tests PRIVATE
        kota::async
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
add_test(NAME unit_tests COMMAND unit_tests)
