add_executable(unit_tests)
set_target_properties(unit_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
)

file(GLOB_RECURSE TEST_SOURCES RELATIVE "${PROJECT_SOURCE_DIR}" CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/tests/*.cpp"
    "${PROJECT_SOURCE_DIR}/tests/*.cc"
)

set(TEST_BASE_SOURCES ${TEST_SOURCES})
list(FILTER TEST_BASE_SOURCES INCLUDE REGEX "^tests/(main\\.cpp|common/.*\\.cpp|reflection/.*\\.cpp|zest/.*\\.cpp)$")
list(TRANSFORM TEST_BASE_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
target_sources(unit_tests PRIVATE ${TEST_BASE_SOURCES})

if(ETD_ENABLE_OPTION)
    set(TEST_OPTION_SOURCES ${TEST_SOURCES})
    list(FILTER TEST_OPTION_SOURCES INCLUDE REGEX "^tests/option/.*\\.cpp$")
    list(TRANSFORM TEST_OPTION_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    target_sources(unit_tests PRIVATE
        ${TEST_OPTION_SOURCES}
    )
else()
    message(STATUS "ETD_ENABLE_OPTION=OFF: skipping tests/option/*")
endif()

if(ETD_ENABLE_DECO AND ETD_ENABLE_OPTION)
    set(TEST_DECO_SOURCES ${TEST_SOURCES})
    list(FILTER TEST_DECO_SOURCES INCLUDE REGEX "^tests/deco/.*\\.(cpp|cc)$")
    list(TRANSFORM TEST_DECO_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    target_sources(unit_tests PRIVATE
        ${TEST_DECO_SOURCES}
    )
elseif(ETD_ENABLE_DECO)
    message(STATUS "ETD_ENABLE_OPTION=OFF: skipping tests/deco/*")
else()
    message(STATUS "ETD_ENABLE_DECO=OFF: skipping tests/deco/*")
endif()

if(ETD_ENABLE_ASYNC)
    set(TEST_EVENTIDE_SOURCES ${TEST_SOURCES})
    list(FILTER TEST_EVENTIDE_SOURCES INCLUDE REGEX "^tests/async/.*\\.cpp$")
    list(TRANSFORM TEST_EVENTIDE_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    target_sources(unit_tests PRIVATE
        ${TEST_EVENTIDE_SOURCES}
    )
else()
    message(STATUS "ETD_ENABLE_ASYNC=OFF: skipping tests/async/*")
endif()

if(ETD_SERDE_ENABLE_SIMDJSON)
    set(TEST_SERDE_SIMDJSON_SOURCES ${TEST_SOURCES})
    list(FILTER TEST_SERDE_SIMDJSON_SOURCES INCLUDE REGEX "^tests/serde/json/simdjson_.*\\.cpp$")
    list(TRANSFORM TEST_SERDE_SIMDJSON_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    target_sources(unit_tests PRIVATE
        ${TEST_SERDE_SIMDJSON_SOURCES}
    )
else()
    message(STATUS "ETD_SERDE_ENABLE_SIMDJSON=OFF: skipping tests/serde/json/*")
endif()

if(ETD_SERDE_ENABLE_YYJSON)
    set(TEST_SERDE_YYJSON_SOURCES ${TEST_SOURCES})
    list(FILTER TEST_SERDE_YYJSON_SOURCES INCLUDE REGEX "^tests/serde/content/.*\\.cpp$")
    list(TRANSFORM TEST_SERDE_YYJSON_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    target_sources(unit_tests PRIVATE
        ${TEST_SERDE_YYJSON_SOURCES}
    )
else()
    message(STATUS "ETD_SERDE_ENABLE_YYJSON=OFF: skipping tests/serde/content/*")
endif()

if(ETD_SERDE_ENABLE_FLATBUFFERS)
    set(TEST_SERDE_FLATBUFFERS_SOURCES ${TEST_SOURCES})
    list(FILTER TEST_SERDE_FLATBUFFERS_SOURCES INCLUDE REGEX "^tests/serde/flatbuffers/.*\\.cpp$")
    list(TRANSFORM TEST_SERDE_FLATBUFFERS_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    target_sources(unit_tests PRIVATE
        ${TEST_SERDE_FLATBUFFERS_SOURCES}
    )
else()
    message(STATUS "ETD_SERDE_ENABLE_FLATBUFFERS=OFF: skipping tests/serde/flatbuffers/*")
endif()

if(ETD_SERDE_ENABLE_TOML)
    set(TEST_SERDE_TOML_SOURCES ${TEST_SOURCES})
    list(FILTER TEST_SERDE_TOML_SOURCES INCLUDE REGEX "^tests/serde/toml/.*\\.cpp$")
    list(TRANSFORM TEST_SERDE_TOML_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    target_sources(unit_tests PRIVATE
        ${TEST_SERDE_TOML_SOURCES}
    )
else()
    message(STATUS "ETD_SERDE_ENABLE_TOML=OFF: skipping tests/serde/toml/*")
endif()

set(TEST_SERDE_BINCODE_SOURCES ${TEST_SOURCES})
list(FILTER TEST_SERDE_BINCODE_SOURCES INCLUDE REGEX "^tests/serde/bincode/.*\\.cpp$")
list(TRANSFORM TEST_SERDE_BINCODE_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
target_sources(unit_tests PRIVATE
    ${TEST_SERDE_BINCODE_SOURCES}
)

if(ETD_ENABLE_ASYNC AND ETD_SERDE_ENABLE_SIMDJSON)
    set(TEST_IPC_SOURCES ${TEST_SOURCES})
    list(FILTER TEST_IPC_SOURCES INCLUDE REGEX "^tests/ipc/.*\\.cpp$")
    list(TRANSFORM TEST_IPC_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
    target_sources(unit_tests PRIVATE
        ${TEST_IPC_SOURCES}
    )
    target_link_libraries(unit_tests PRIVATE
        eventide::ipc
        eventide::ipc::lsp
    )
else()
    message(STATUS "ETD_ENABLE_ASYNC=OFF or ETD_SERDE_ENABLE_SIMDJSON=OFF: skipping tests/ipc/*")
endif()

target_include_directories(unit_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/examples/build_system"
)

target_link_libraries(unit_tests PRIVATE
    eventide::serde
    eventide::zest
    cpptrace::cpptrace
)

if(ETD_ENABLE_OPTION)
    target_link_libraries(unit_tests PRIVATE
        eventide::option
    )
endif()

if(ETD_ENABLE_DECO AND ETD_ENABLE_OPTION)
    target_link_libraries(unit_tests PRIVATE
        eventide::deco
    )
endif()

if(ETD_ENABLE_ASYNC)
    target_link_libraries(unit_tests PRIVATE
        eventide::async
    )
endif()

eventide_apply_project_options(unit_tests)
add_test(NAME unit_tests COMMAND unit_tests)
