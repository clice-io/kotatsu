include(FetchContent)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

function(eventide_apply_cache_options)
    set(option_pairs ${ARGV})
    list(LENGTH option_pairs option_count)
    math(EXPR option_remainder "${option_count} % 2")
    if(NOT option_remainder EQUAL 0)
        message(FATAL_ERROR "eventide_apply_cache_options expects key/value pairs.")
    endif()

    while(option_pairs)
        list(POP_FRONT option_pairs option_name option_value)
        set(${option_name} "${option_value}" CACHE INTERNAL "" FORCE)
    endwhile()
endfunction()

function(eventide_ensure_cpm)
    if(NOT ET_USE_CPM_FOR_TESTS OR NOT ET_ENABLE_TEST)
        return()
    endif()

    if(COMMAND CPMAddPackage)
        return()
    endif()

    set(cpm_version "0.42.1")
    set(cpm_path "${CMAKE_BINARY_DIR}/cmake/CPM_${cpm_version}.cmake")

    if(NOT EXISTS "${cpm_path}")
        file(DOWNLOAD
            "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${cpm_version}/CPM.cmake"
            "${cpm_path}"
            TLS_VERIFY ON
            STATUS cpm_download_status
        )
        list(GET cpm_download_status 0 cpm_status_code)
        if(NOT cpm_status_code EQUAL 0)
            list(GET cpm_download_status 1 cpm_status_string)
            message(FATAL_ERROR "Failed to download CPM.cmake: ${cpm_status_string}")
        endif()
    endif()

    include("${cpm_path}")
endfunction()

function(eventide_silence_third_party_target target)
    if(NOT TARGET ${target})
        return()
    endif()

    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        return()
    endif()

    if(MSVC)
        get_target_property(target_compile_options ${target} COMPILE_OPTIONS)
        if(target_compile_options AND NOT target_compile_options STREQUAL "NOTFOUND")
            set(filtered_compile_options ${target_compile_options})
            list(FILTER filtered_compile_options EXCLUDE REGEX [[^[-/](W[0-4]|Wall)$]])
            set_property(TARGET ${target} PROPERTY COMPILE_OPTIONS "${filtered_compile_options}")
        endif()
    endif()

    target_compile_options(${target} PRIVATE
        $<$<BOOL:${MSVC}>:/W0>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>
    )
endfunction()

function(eventide_silence_dependency_warnings name)
    if(name STREQUAL "simdjson")
        eventide_silence_third_party_target(simdjson)
        eventide_silence_third_party_target(simdjson_static)
    elseif(name STREQUAL "yyjson")
        eventide_silence_third_party_target(yyjson)
    elseif(name STREQUAL "flatbuffers")
        eventide_silence_third_party_target(flatbuffers)
        eventide_silence_third_party_target(flatbuffers_shared)
    elseif(name STREQUAL "libuv")
        eventide_silence_third_party_target(uv)
        eventide_silence_third_party_target(uv_a)
    elseif(name STREQUAL "cpptrace")
        eventide_silence_third_party_target(cpptrace-lib)
        eventide_silence_third_party_target(dwarf)
        eventide_silence_third_party_target(libzstd_static)
        eventide_silence_third_party_target(libzstd_shared)
        eventide_silence_third_party_target(zstd)
    endif()
endfunction()

function(eventide_add_git_dependency name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "GIT_REPOSITORY;GIT_TAG" "OPTIONS")
    if(NOT ARG_GIT_REPOSITORY)
        message(FATAL_ERROR "eventide_add_git_dependency(${name}) requires GIT_REPOSITORY.")
    endif()
    if(NOT ARG_GIT_TAG)
        message(FATAL_ERROR "eventide_add_git_dependency(${name}) requires GIT_TAG.")
    endif()

    if(ET_USE_CPM_FOR_TESTS AND ET_ENABLE_TEST)
        eventide_ensure_cpm()
        set(cpm_options "")
        set(option_pairs ${ARG_OPTIONS})
        list(LENGTH option_pairs option_count)
        math(EXPR option_remainder "${option_count} % 2")
        if(NOT option_remainder EQUAL 0)
            message(FATAL_ERROR "eventide_add_git_dependency(${name}) OPTIONS expects key/value pairs.")
        endif()
        while(option_pairs)
            list(POP_FRONT option_pairs option_name option_value)
            list(APPEND cpm_options "${option_name} ${option_value}")
        endwhile()

        if(ARG_OPTIONS)
            CPMAddPackage(
                NAME ${name}
                GIT_REPOSITORY ${ARG_GIT_REPOSITORY}
                GIT_TAG ${ARG_GIT_TAG}
                GIT_SHALLOW TRUE
                OPTIONS ${cpm_options}
            )
        else()
            CPMAddPackage(
                NAME ${name}
                GIT_REPOSITORY ${ARG_GIT_REPOSITORY}
                GIT_TAG ${ARG_GIT_TAG}
                GIT_SHALLOW TRUE
            )
        endif()
        eventide_silence_dependency_warnings(${name})
    else()
        eventide_apply_cache_options(${ARG_OPTIONS})
        FetchContent_Declare(
            ${name}
            GIT_REPOSITORY ${ARG_GIT_REPOSITORY}
            GIT_TAG ${ARG_GIT_TAG}
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(${name})
        eventide_silence_dependency_warnings(${name})
    endif()
endfunction()
