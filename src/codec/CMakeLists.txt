add_library(kota_codec INTERFACE)
add_library(kota::codec ALIAS kota_codec)

target_include_directories(kota_codec INTERFACE
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(kota_codec INTERFACE
    kota::meta
)

kota_apply_project_options(kota_codec)

if(KOTA_CODEC_ENABLE_YYJSON AND NOT KOTA_CODEC_ENABLE_SIMDJSON)
    message(FATAL_ERROR "KOTA_CODEC_ENABLE_YYJSON requires KOTA_CODEC_ENABLE_SIMDJSON.")
endif()

if(KOTA_CODEC_ENABLE_SIMDJSON)
    # Keep simdjson in "library consumer" mode.
    set(SIMDJSON_BUILD_STATIC_LIB OFF CACHE BOOL "" FORCE)
    set(SIMDJSON_DEVELOPER_MODE OFF CACHE BOOL "" FORCE)
    set(SIMDJSON_DEVELOPMENT_CHECKS OFF CACHE BOOL "" FORCE)
    set(SIMDJSON_STATIC_REFLECTION OFF CACHE BOOL "" FORCE)

    # Lock behavior knobs so parent/toolchain cache does not accidentally change them.
    set(SIMDJSON_ENABLE_THREADS ON CACHE BOOL "" FORCE)
    set(SIMDJSON_EXCEPTIONS ${KOTA_ENABLE_EXCEPTIONS} CACHE BOOL "" FORCE)
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

    kota_add_git_dependency(
        simdjson
        GIT_REPOSITORY https://github.com/simdjson/simdjson.git
        GIT_TAG v4.2.4
        OPTIONS
            SIMDJSON_BUILD_STATIC_LIB OFF
            SIMDJSON_DEVELOPER_MODE OFF
            SIMDJSON_DEVELOPMENT_CHECKS OFF
            SIMDJSON_STATIC_REFLECTION OFF
            SIMDJSON_ENABLE_THREADS ON
            SIMDJSON_EXCEPTIONS ${KOTA_ENABLE_EXCEPTIONS}
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

    add_library(kota_codec_json INTERFACE)
    add_library(kota::codec::json ALIAS kota_codec_json)

    target_include_directories(kota_codec_json INTERFACE
        "${PROJECT_SOURCE_DIR}/include"
    )

    target_link_libraries(kota_codec_json INTERFACE
        kota::meta
        simdjson::simdjson
    )

    kota_apply_project_options(kota_codec_json)

    if(KOTA_CODEC_ENABLE_YYJSON)
        # Keep yyjson as a lean dependency-only library.
        set(YYJSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(YYJSON_BUILD_FUZZER OFF CACHE BOOL "" FORCE)
        set(YYJSON_BUILD_MISC OFF CACHE BOOL "" FORCE)
        set(YYJSON_BUILD_DOC OFF CACHE BOOL "" FORCE)
        set(YYJSON_INSTALL OFF CACHE BOOL "" FORCE)

        kota_add_git_dependency(
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

        target_link_libraries(kota_codec_json INTERFACE
            yyjson
        )
    endif()

    target_link_libraries(kota_codec INTERFACE
        kota::codec::json
    )
endif()

if(KOTA_CODEC_ENABLE_FLATBUFFERS)
    set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_FLATC OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_FLATLIB ON CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_SHAREDLIB OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_INSTALL OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_STRICT_MODE OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_CPP17 OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_LEGACY OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_ENABLE_PCH OFF CACHE BOOL "" FORCE)

    kota_add_git_dependency(
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

    add_library(kota_codec_flatbuffers INTERFACE)
    add_library(kota::codec::flatbuffers ALIAS kota_codec_flatbuffers)

    target_include_directories(kota_codec_flatbuffers INTERFACE
        "${PROJECT_SOURCE_DIR}/include"
    )

    target_link_libraries(kota_codec_flatbuffers INTERFACE
        kota::meta
        flatbuffers
    )

    kota_apply_project_options(kota_codec_flatbuffers)

    target_link_libraries(kota_codec INTERFACE
        kota::codec::flatbuffers
    )
endif()

if(KOTA_CODEC_ENABLE_TOML)
    set(TOMLPLUSPLUS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(TOMLPLUSPLUS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(TOMLPLUSPLUS_BUILD_MODULES OFF CACHE BOOL "" FORCE)
    set(TOMLPLUSPLUS_INSTALL OFF CACHE BOOL "" FORCE)

    kota_add_git_dependency(
        tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
        OPTIONS
            TOMLPLUSPLUS_BUILD_TESTS OFF
            TOMLPLUSPLUS_BUILD_EXAMPLES OFF
            TOMLPLUSPLUS_BUILD_MODULES OFF
            TOMLPLUSPLUS_INSTALL OFF
    )

    add_library(kota_codec_toml INTERFACE)
    add_library(kota::codec::toml ALIAS kota_codec_toml)

    target_include_directories(kota_codec_toml INTERFACE
        "${PROJECT_SOURCE_DIR}/include"
    )

    target_link_libraries(kota_codec_toml INTERFACE
        kota::meta
        tomlplusplus::tomlplusplus
    )

    target_compile_definitions(kota_codec_toml INTERFACE
        TOML_EXCEPTIONS=$<IF:$<BOOL:${KOTA_ENABLE_EXCEPTIONS}>,1,0>
    )

    kota_apply_project_options(kota_codec_toml)

    target_link_libraries(kota_codec INTERFACE
        kota::codec::toml
    )
endif()
