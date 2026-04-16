include(FetchContent)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

function(kota_validate_option_pairs context)
    set(option_pairs ${ARGN})
    list(LENGTH option_pairs option_count)
    math(EXPR option_remainder "${option_count} % 2")
    if(NOT option_remainder EQUAL 0)
        message(FATAL_ERROR "${context} expects key/value pairs.")
    endif()
endfunction()

function(kota_apply_cache_options)
    set(option_pairs ${ARGV})
    kota_validate_option_pairs("kota_apply_cache_options" ${option_pairs})

    while(option_pairs)
        list(POP_FRONT option_pairs option_name option_value)
        set(${option_name} "${option_value}" CACHE INTERNAL "" FORCE)
    endwhile()
endfunction()

function(kota_make_cpm_options output_var context)
    set(option_pairs ${ARGN})
    kota_validate_option_pairs("${context}" ${option_pairs})

    set(cpm_options "")
    while(option_pairs)
        list(POP_FRONT option_pairs option_name option_value)
        list(APPEND cpm_options "${option_name} ${option_value}")
    endwhile()

    set(${output_var} "${cpm_options}" PARENT_SCOPE)
endfunction()

function(kota_ensure_cpm)
    if(NOT KOTA_USE_CPM_FOR_TESTS OR NOT KOTA_ENABLE_TEST)
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

function(kota_silence_third_party_target target)
    if(NOT TARGET ${target})
        return()
    endif()

    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        return()
    endif()

    target_compile_options(${target} PRIVATE
        $<$<BOOL:${MSVC}>:/W0>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>
    )
endfunction()

function(kota_silence_dependency_warnings name)
    if(name STREQUAL "simdjson")
        kota_silence_third_party_target(simdjson)
        kota_silence_third_party_target(simdjson_static)
    elseif(name STREQUAL "yyjson")
        kota_silence_third_party_target(yyjson)
    elseif(name STREQUAL "flatbuffers")
        kota_silence_third_party_target(flatbuffers)
        kota_silence_third_party_target(flatbuffers_shared)
    elseif(name STREQUAL "libuv")
        kota_silence_third_party_target(uv)
        kota_silence_third_party_target(uv_a)
    elseif(name STREQUAL "cpptrace")
        kota_silence_third_party_target(cpptrace-lib)
        kota_silence_third_party_target(dwarf)
        kota_silence_third_party_target(libzstd_static)
        kota_silence_third_party_target(libzstd_shared)
        kota_silence_third_party_target(zstd)
    endif()
endfunction()

function(kota_add_git_dependency name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "GIT_REPOSITORY;GIT_TAG" "OPTIONS")
    if(NOT ARG_GIT_REPOSITORY)
        message(FATAL_ERROR "kota_add_git_dependency(${name}) requires GIT_REPOSITORY.")
    endif()
    if(NOT ARG_GIT_TAG)
        message(FATAL_ERROR "kota_add_git_dependency(${name}) requires GIT_TAG.")
    endif()

    if(KOTA_USE_CPM_FOR_TESTS AND KOTA_ENABLE_TEST)
        kota_ensure_cpm()
        kota_make_cpm_options(cpm_options "kota_add_git_dependency(${name}) OPTIONS" ${ARG_OPTIONS})

        set(cpm_args
            NAME ${name}
            GIT_REPOSITORY ${ARG_GIT_REPOSITORY}
            GIT_TAG ${ARG_GIT_TAG}
            GIT_SHALLOW TRUE
        )
        if(cpm_options)
            list(APPEND cpm_args OPTIONS ${cpm_options})
        endif()

        CPMAddPackage(${cpm_args})
        kota_silence_dependency_warnings(${name})
    else()
        kota_apply_cache_options(${ARG_OPTIONS})
        FetchContent_Declare(
            ${name}
            GIT_REPOSITORY ${ARG_GIT_REPOSITORY}
            GIT_TAG ${ARG_GIT_TAG}
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(${name})
        kota_silence_dependency_warnings(${name})
    endif()
endfunction()
