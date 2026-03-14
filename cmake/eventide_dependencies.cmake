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
    if(NOT EVENTIDE_USE_CPM_FOR_TESTS OR NOT EVENTIDE_ENABLE_TEST)
        return()
    endif()

    if(COMMAND CPMAddPackage)
        return()
    endif()

    set(cpm_version "0.40.5")
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

function(eventide_add_git_dependency name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "GIT_REPOSITORY;GIT_TAG" "OPTIONS")
    if(NOT ARG_GIT_REPOSITORY)
        message(FATAL_ERROR "eventide_add_git_dependency(${name}) requires GIT_REPOSITORY.")
    endif()
    if(NOT ARG_GIT_TAG)
        message(FATAL_ERROR "eventide_add_git_dependency(${name}) requires GIT_TAG.")
    endif()

    if(EVENTIDE_USE_CPM_FOR_TESTS AND EVENTIDE_ENABLE_TEST)
        eventide_ensure_cpm()
        set(cpm_options "")
        set(option_pairs ${ARG_OPTIONS})
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
    else()
        eventide_apply_cache_options(${ARG_OPTIONS})
        FetchContent_Declare(
            ${name}
            GIT_REPOSITORY ${ARG_GIT_REPOSITORY}
            GIT_TAG ${ARG_GIT_TAG}
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(${name})
    endif()
endfunction()
