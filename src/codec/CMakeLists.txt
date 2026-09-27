add_library(kota_codec INTERFACE)
add_library(kota::codec ALIAS kota_codec)

target_link_libraries(kota_codec INTERFACE
    kota::meta
)

# Declares kota::codec::<backend>: the codec headers plus the backend's
# third-party dependency (fetched through kota_add_git_dependency, whose
# OPTIONS pin the dependency's build knobs), and adds it to kota::codec.
function(kota_add_codec_backend backend)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "LINK;DEFINITIONS")

    add_library(kota_codec_${backend} INTERFACE)
    add_library(kota::codec::${backend} ALIAS kota_codec_${backend})

    target_link_libraries(kota_codec_${backend} INTERFACE
        kota::meta
        ${ARG_LINK}
    )
    if(ARG_DEFINITIONS)
        target_compile_definitions(kota_codec_${backend} INTERFACE ${ARG_DEFINITIONS})
    endif()

    target_link_libraries(kota_codec INTERFACE
        kota::codec::${backend}
    )
endfunction()

if(KOTA_CODEC_ENABLE_SIMDJSON)
    kota_add_git_dependency(
        simdjson
        GIT_REPOSITORY https://github.com/simdjson/simdjson.git
        GIT_TAG v4.2.4
        OPTIONS
            # Library consumer mode.
            SIMDJSON_BUILD_STATIC_LIB OFF
            SIMDJSON_DEVELOPER_MODE OFF
            SIMDJSON_DEVELOPMENT_CHECKS OFF
            SIMDJSON_STATIC_REFLECTION OFF
            # Behavior knobs, locked so a parent or toolchain cache cannot
            # change them.
            SIMDJSON_ENABLE_THREADS ON
            SIMDJSON_EXCEPTIONS ${KOTA_ENABLE_EXCEPTIONS}
            SIMDJSON_DISABLE_DEPRECATED_API OFF
            SIMDJSON_MINUS_ZERO_AS_FLOAT OFF
            # Development and sanitizer toggles.
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
    kota_add_codec_backend(json LINK simdjson::simdjson)
endif()

if(KOTA_CODEC_ENABLE_FLATBUFFERS)
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
    kota_add_codec_backend(flatbuffers LINK flatbuffers)
endif()

if(KOTA_CODEC_ENABLE_TOML)
    # toml++ v3.4.0 builds nothing but its header-only target when it is not
    # the top-level project, so it takes no options.
    kota_add_git_dependency(
        tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
    )
    # toml++ is pinned to no-exceptions mode unconditionally (independent of
    # KOTA_ENABLE_EXCEPTIONS); kota/codec/toml/type.h enforces the same pin for
    # any build system that does not pass this definition. See PR #168.
    kota_add_codec_backend(toml
        LINK tomlplusplus::tomlplusplus
        DEFINITIONS TOML_EXCEPTIONS=0
    )
endif()
