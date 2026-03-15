add_library(eventide_serde INTERFACE)
add_library(eventide::serde ALIAS eventide_serde)

target_include_directories(eventide_serde INTERFACE
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(eventide_serde INTERFACE
    eventide::reflection
)

eventide_apply_project_options(eventide_serde)

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
    set(SIMDJSON_EXCEPTIONS ${EVENTIDE_ENABLE_EXCEPTIONS} CACHE BOOL "" FORCE)
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

    eventide_add_git_dependency(
        simdjson
        GIT_REPOSITORY https://github.com/simdjson/simdjson.git
        GIT_TAG v4.2.4
        OPTIONS
            SIMDJSON_BUILD_STATIC_LIB OFF
            SIMDJSON_DEVELOPER_MODE OFF
            SIMDJSON_DEVELOPMENT_CHECKS OFF
            SIMDJSON_STATIC_REFLECTION OFF
            SIMDJSON_ENABLE_THREADS ON
            SIMDJSON_EXCEPTIONS ${EVENTIDE_ENABLE_EXCEPTIONS}
            SIMDJSON_DISABLE_DEPRECATED_API OFF
            SIMDJSON_MINUS_ZERO_AS_FLOAT OFF
            SIMDJSON_CHECK_EOF OFF
            SIMDJSON_SANITIZE OFF
            SIMDJSON_SANITIZE_UNDEFINED OFF
            SIMDJSON_SANITIZE_MEMORY OFF
            SIMDJSON_SANITIZE_THREADS OFF
            SIMDJSON_GLIBCXX_ASSERTIONS OFF
            SIMDJSON_USE_LIBCPP OFF
            SIMDJSON_VERBOSE_LOGGING OFF
            SIMDJSON_SKIPUTF8VALIDATION OFF
    )

    add_library(eventide_serde_json INTERFACE)
    add_library(eventide::serde::json ALIAS eventide_serde_json)

    target_include_directories(eventide_serde_json INTERFACE
        "${PROJECT_SOURCE_DIR}/include"
    )

    target_link_libraries(eventide_serde_json INTERFACE
        eventide::reflection
        simdjson::simdjson
    )

    eventide_apply_project_options(eventide_serde_json)

    if(EVENTIDE_SERDE_ENABLE_YYJSON)
        # Keep yyjson as a lean dependency-only library.
        set(YYJSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(YYJSON_BUILD_FUZZER OFF CACHE BOOL "" FORCE)
        set(YYJSON_BUILD_MISC OFF CACHE BOOL "" FORCE)
        set(YYJSON_BUILD_DOC OFF CACHE BOOL "" FORCE)
        set(YYJSON_INSTALL OFF CACHE BOOL "" FORCE)

        eventide_add_git_dependency(
            yyjson
            GIT_REPOSITORY https://github.com/ibireme/yyjson.git
            GIT_TAG 0.12.0
            OPTIONS
                YYJSON_BUILD_TESTS OFF
                YYJSON_BUILD_FUZZER OFF
                YYJSON_BUILD_MISC OFF
                YYJSON_BUILD_DOC OFF
                YYJSON_INSTALL OFF
        )

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
    set(FLATBUFFERS_STRICT_MODE OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_CPP17 OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_LEGACY OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_ENABLE_PCH OFF CACHE BOOL "" FORCE)

    eventide_add_git_dependency(
        flatbuffers
        GIT_REPOSITORY https://github.com/google/flatbuffers.git
        GIT_TAG v25.2.10
        OPTIONS
            FLATBUFFERS_BUILD_TESTS OFF
            FLATBUFFERS_BUILD_FLATC OFF
            FLATBUFFERS_BUILD_FLATLIB ON
            FLATBUFFERS_BUILD_SHAREDLIB OFF
            FLATBUFFERS_INSTALL OFF
            FLATBUFFERS_STRICT_MODE OFF
            FLATBUFFERS_BUILD_CPP17 OFF
            FLATBUFFERS_BUILD_LEGACY OFF
            FLATBUFFERS_ENABLE_PCH OFF
    )

    add_library(eventide_serde_flatbuffers INTERFACE)
    add_library(eventide::serde::flatbuffers ALIAS eventide_serde_flatbuffers)

    target_include_directories(eventide_serde_flatbuffers INTERFACE
        "${PROJECT_SOURCE_DIR}/include"
    )

    target_link_libraries(eventide_serde_flatbuffers INTERFACE
        eventide::reflection
        flatbuffers
    )

    eventide_apply_project_options(eventide_serde_flatbuffers)

    target_link_libraries(eventide_serde INTERFACE
        eventide::serde::flatbuffers
    )
endif()

if(EVENTIDE_SERDE_ENABLE_TOML)
    set(TOMLPLUSPLUS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(TOMLPLUSPLUS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(TOMLPLUSPLUS_BUILD_MODULES OFF CACHE BOOL "" FORCE)
    set(TOMLPLUSPLUS_INSTALL OFF CACHE BOOL "" FORCE)

    eventide_add_git_dependency(
        tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
        OPTIONS
            TOMLPLUSPLUS_BUILD_TESTS OFF
            TOMLPLUSPLUS_BUILD_EXAMPLES OFF
            TOMLPLUSPLUS_BUILD_MODULES OFF
            TOMLPLUSPLUS_INSTALL OFF
    )

    add_library(eventide_serde_toml INTERFACE)
    add_library(eventide::serde::toml ALIAS eventide_serde_toml)

    target_include_directories(eventide_serde_toml INTERFACE
        "${PROJECT_SOURCE_DIR}/include"
    )

    target_link_libraries(eventide_serde_toml INTERFACE
        eventide::reflection
        tomlplusplus::tomlplusplus
    )

    target_compile_definitions(eventide_serde_toml INTERFACE
        TOML_EXCEPTIONS=$<IF:$<BOOL:${EVENTIDE_ENABLE_EXCEPTIONS}>,1,0>
    )

    eventide_apply_project_options(eventide_serde_toml)

    target_link_libraries(eventide_serde INTERFACE
        eventide::serde::toml
    )
endif()
