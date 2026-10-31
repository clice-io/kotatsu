include(FetchContent)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

function(kotatsu_validate_option_pairs context)
    set(option_pairs ${ARGN})
    list(LENGTH option_pairs option_count)
    math(EXPR option_remainder "${option_count} % 2")
    if(NOT option_remainder EQUAL 0)
        message(FATAL_ERROR "${context} expects key/value pairs.")
    endif()
endfunction()

function(kotatsu_apply_cache_options)
    set(option_pairs ${ARGV})
    kotatsu_validate_option_pairs("kotatsu_apply_cache_options" ${option_pairs})

    while(option_pairs)
        list(POP_FRONT option_pairs option_name option_value)
        set(${option_name} "${option_value}" CACHE INTERNAL "" FORCE)
    endwhile()
endfunction()

function(kotatsu_make_cpm_options output_var context)
    set(option_pairs ${ARGN})
    kotatsu_validate_option_pairs("${context}" ${option_pairs})

    set(cpm_options "")
    while(option_pairs)
        list(POP_FRONT option_pairs option_name option_value)
        list(APPEND cpm_options "${option_name} ${option_value}")
    endwhile()

    set(${output_var} "${cpm_options}" PARENT_SCOPE)
endfunction()

function(kotatsu_ensure_cpm)
    if(NOT KOTATSU_USE_CPM_FOR_TESTS OR NOT KOTATSU_ENABLE_TEST)
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

function(kotatsu_silence_third_party_target target)
    if(NOT TARGET ${target})
        return()
    endif()

    # Consumers of this target see its include dirs as SYSTEM, so warnings from
    # the third-party headers do not trip our /WX or -Werror in translation units
    # that only include (but don't compile) the dependency.
    get_target_property(existing_includes ${target} INTERFACE_INCLUDE_DIRECTORIES)
    if(existing_includes)
        set_target_properties(${target} PROPERTIES
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${existing_includes}")
    endif()

    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        return()
    endif()

    # Do NOT clear the target's existing COMPILE_OPTIONS: it was initialized
    # from directory COMPILE_OPTIONS at target creation and includes the
    # sanitizer flags (/fsanitize=address, -fsanitize=thread, ...) we pushed
    # via add_compile_options at the top level. Clearing would strip those
    # and leave dependencies uninstrumented. Instead, append /W0 / -w below;
    # MSVC/GCC/Clang all honour last-wins so the dependency's own /W4 / -Wall
    # gets overridden.
    target_compile_options(${target} PRIVATE
        $<$<BOOL:${MSVC}>:/W0>
        $<$<BOOL:${MSVC}>:/WX->
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-error>
    )
endfunction()

function(kotatsu_silence_dependency_warnings name)
    if(name STREQUAL "simdjson")
        kotatsu_silence_third_party_target(simdjson)
        kotatsu_silence_third_party_target(simdjson_static)
    elseif(name STREQUAL "flatbuffers")
        kotatsu_silence_third_party_target(flatbuffers)
        kotatsu_silence_third_party_target(flatbuffers_shared)
    elseif(name STREQUAL "libuv")
        kotatsu_silence_third_party_target(uv)
        kotatsu_silence_third_party_target(uv_a)
    elseif(name STREQUAL "cpptrace")
        kotatsu_silence_third_party_target(cpptrace-lib)
        kotatsu_silence_third_party_target(dwarf)
        kotatsu_silence_third_party_target(libzstd_static)
        kotatsu_silence_third_party_target(libzstd_shared)
        kotatsu_silence_third_party_target(zstd)
    endif()
endfunction()

function(kotatsu_add_git_dependency name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "GIT_REPOSITORY;GIT_TAG" "OPTIONS")
    if(NOT ARG_GIT_REPOSITORY)
        message(FATAL_ERROR "kotatsu_add_git_dependency(${name}) requires GIT_REPOSITORY.")
    endif()
    if(NOT ARG_GIT_TAG)
        message(FATAL_ERROR "kotatsu_add_git_dependency(${name}) requires GIT_TAG.")
    endif()

    # A parent project that already bootstrapped CPM (and usually a
    # CPM_SOURCE_CACHE) gets kotatsu's dependencies through it as well.
    if((KOTATSU_USE_CPM_FOR_TESTS AND KOTATSU_ENABLE_TEST) OR COMMAND CPMAddPackage)
        kotatsu_ensure_cpm()
        kotatsu_make_cpm_options(cpm_options "kotatsu_add_git_dependency(${name}) OPTIONS" ${ARG_OPTIONS})

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
        kotatsu_silence_dependency_warnings(${name})
    else()
        kotatsu_apply_cache_options(${ARG_OPTIONS})
        FetchContent_Declare(
            ${name}
            GIT_REPOSITORY ${ARG_GIT_REPOSITORY}
            GIT_TAG ${ARG_GIT_TAG}
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(${name})
        kotatsu_silence_dependency_warnings(${name})
    endif()
endfunction()
