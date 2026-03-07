add_library(eventide_serde INTERFACE)
add_library(eventide::serde ALIAS eventide_serde)

target_include_directories(eventide_serde INTERFACE
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(eventide_serde INTERFACE
    eventide::reflection
)

if(EVENTIDE_SERDE_ENABLE_YYJSON AND NOT EVENTIDE_SERDE_ENABLE_SIMDJSON)
    message(FATAL_ERROR "EVENTIDE_SERDE_ENABLE_YYJSON requires EVENTIDE_SERDE_ENABLE_SIMDJSON.")
endif()

if(EVENTIDE_SERDE_ENABLE_SIMDJSON)
    # Keep simdjson in "library consumer" mode.
    set(SIMDJSON_BUILD_STATIC_LIB OFF CACHE BOOL "" FORCE)
    set(SIMDJSON_DEVELOPER_MODE OFF CACHE BOOL "" FORCE)
    set(SIMDJSON_DEVELOPMENT_CHECKS OFF CACHE BOOL "" FORCE)
    set(SIMDJSON_STATIC_REFLECTION OFF CACHE BOOL "" FORCE)

    # Lock behavior knobs so parent/toolchain cache does not accidentally change them.
    set(SIMDJSON_ENABLE_THREADS ON CACHE BOOL "" FORCE)
    set(SIMDJSON_EXCEPTIONS ON CACHE BOOL "" FORCE)
    set(SIMDJSON_DISABLE_DEPRECATED_API OFF CACHE BOOL "" FORCE)
    set(SIMDJSON_MINUS_ZERO_AS_FLOAT OFF CACHE BOOL "" FORCE)

    # Keep development / sanitizer toggles off.
    set(SIMDJSON_CHECK_EOF OFF CACHE BOOL "" FORCE)
    set(SIMDJSON_SANITIZE OFF CACHE BOOL "" FORCE)
    set(SIMDJSON_SANITIZE_UNDEFINED OFF CACHE BOOL "" FORCE)
    set(SIMDJSON_SANITIZE_MEMORY OFF CACHE BOOL "" FORCE)
    set(SIMDJSON_SANITIZE_THREADS OFF CACHE BOOL "" FORCE)
    set(SIMDJSON_GLIBCXX_ASSERTIONS OFF CACHE BOOL "" FORCE)
    set(SIMDJSON_USE_LIBCPP OFF CACHE BOOL "" FORCE)
    set(SIMDJSON_VERBOSE_LOGGING OFF CACHE BOOL "" FORCE)
    set(SIMDJSON_SKIPUTF8VALIDATION OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        simdjson
        GIT_REPOSITORY https://github.com/simdjson/simdjson.git
        GIT_TAG v4.2.4
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(simdjson)

    add_library(eventide_serde_json INTERFACE)
    add_library(eventide::serde::json ALIAS eventide_serde_json)

    target_include_directories(eventide_serde_json INTERFACE
        "${PROJECT_SOURCE_DIR}/include"
    )

    target_link_libraries(eventide_serde_json INTERFACE
        eventide::reflection
        simdjson::simdjson
    )

    if(EVENTIDE_SERDE_ENABLE_YYJSON)
        # Keep yyjson as a lean dependency-only library.
        set(YYJSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(YYJSON_BUILD_FUZZER OFF CACHE BOOL "" FORCE)
        set(YYJSON_BUILD_MISC OFF CACHE BOOL "" FORCE)
        set(YYJSON_BUILD_DOC OFF CACHE BOOL "" FORCE)
        set(YYJSON_INSTALL OFF CACHE BOOL "" FORCE)

        FetchContent_Declare(
            yyjson
            GIT_REPOSITORY https://github.com/ibireme/yyjson.git
            GIT_TAG 0.12.0
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(yyjson)

        target_link_libraries(eventide_serde_json INTERFACE
            yyjson
        )
    endif()

    target_link_libraries(eventide_serde INTERFACE
        eventide::serde::json
    )
endif()

if(EVENTIDE_SERDE_ENABLE_FLATBUFFERS)
    set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_FLATC OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_FLATLIB ON CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_SHAREDLIB OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        flatbuffers
        GIT_REPOSITORY https://github.com/google/flatbuffers.git
        GIT_TAG v25.2.10
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(flatbuffers)

    add_library(eventide_serde_flatbuffers INTERFACE)
    add_library(eventide::serde::flatbuffers ALIAS eventide_serde_flatbuffers)

    target_include_directories(eventide_serde_flatbuffers INTERFACE
        "${PROJECT_SOURCE_DIR}/include"
    )

    target_link_libraries(eventide_serde_flatbuffers INTERFACE
        eventide::reflection
        flatbuffers
    )

    target_link_libraries(eventide_serde INTERFACE
        eventide::serde::flatbuffers
    )
endif()

if(EVENTIDE_SERDE_ENABLE_TOML)
    set(TOMLPLUSPLUS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(TOMLPLUSPLUS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(TOMLPLUSPLUS_BUILD_MODULES OFF CACHE BOOL "" FORCE)
    set(TOMLPLUSPLUS_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(tomlplusplus)

    add_library(eventide_serde_toml INTERFACE)
    add_library(eventide::serde::toml ALIAS eventide_serde_toml)

    target_include_directories(eventide_serde_toml INTERFACE
        "${PROJECT_SOURCE_DIR}/include"
    )

    target_link_libraries(eventide_serde_toml INTERFACE
        eventide::reflection
        tomlplusplus::tomlplusplus
    )

    target_link_libraries(eventide_serde INTERFACE
        eventide::serde::toml
    )
endif()
