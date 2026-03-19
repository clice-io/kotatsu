cmake_minimum_required(VERSION 3.16)

project(eventide LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

option(ETD_ENABLE_ASYNC "Enable async eventide target and libuv dependency" ON)
option(ETD_ENABLE_OPTION "Enable option parser target" ON)
option(ETD_ENABLE_DECO "Enable deco target" ON)
option(ETD_ENABLE_TEST "Build unit tests" OFF)
option(ETD_ENABLE_EXAMPLES "Build examples" OFF)
option(ETD_ENABLE_ZEST "Build zest test framework target" OFF)
option(ETD_SERDE_ENABLE_SIMDJSON "Enable simdjson dependency for eventide::serde" OFF)
option(ETD_SERDE_ENABLE_YYJSON "Enable yyjson dependency for eventide::serde" OFF)
option(ETD_SERDE_ENABLE_FLATBUFFERS "Enable flatbuffers dependency for eventide::serde" OFF)
option(ETD_SERDE_ENABLE_TOML "Enable tomlplusplus dependency for eventide::serde" OFF)
option(ETD_BUILD_ALL_TESTS "Enable all optional test suites for CI" OFF)
option(ETD_ENABLE_EXCEPTIONS "Build eventide targets with exception support" ON)
option(ETD_ENABLE_RTTI "Build eventide targets with RTTI support" ON)
option(ETD_USE_CPM_FOR_TESTS "Use CPM.cmake instead of FetchContent when tests are enabled" OFF)
option(ETD_ENABLE_CCACHE "Enable compiler cache launchers when available" OFF)
option(ETD_SANITIZE_ADDRESS "Enable AddressSanitizer" OFF)
option(ETD_SANITIZE_MEMORY "Enable MemorySanitizer" OFF)
option(ETD_SANITIZE_THREAD "Enable ThreadSanitizer" OFF)
option(ETD_SANITIZE_UNDEFINED "Enable UndefinedBehaviorSanitizer" OFF)

if(MSVC)
    set(eventide_msvc_flag_vars
        CMAKE_CXX_FLAGS
        CMAKE_CXX_FLAGS_DEBUG
        CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_RELWITHDEBINFO
        CMAKE_CXX_FLAGS_MINSIZEREL
    )

    if(ETD_ENABLE_CCACHE)
        foreach(flag_var
                CMAKE_C_FLAGS_DEBUG
                CMAKE_CXX_FLAGS_DEBUG
                CMAKE_C_FLAGS_RELWITHDEBINFO
                CMAKE_CXX_FLAGS_RELWITHDEBINFO)
            if(DEFINED ${flag_var})
                string(REPLACE "/Zi" "/Z7" ${flag_var} "${${flag_var}}")
                set(${flag_var} "${${flag_var}}" CACHE STRING "" FORCE)
            endif()
        endforeach()
    endif()

    if(NOT ETD_ENABLE_EXCEPTIONS)
        foreach(flag_var ${eventide_msvc_flag_vars})
            if(DEFINED ${flag_var})
                set(eventide_flags "${${flag_var}}")
                string(REGEX REPLACE "(^| )[/-]EH[a-zA-Z0-9-]+( |$)" " " eventide_flags "${eventide_flags}")
                string(REGEX REPLACE " +" " " eventide_flags "${eventide_flags}")
                string(STRIP "${eventide_flags}" eventide_flags)
                set(${flag_var} "${eventide_flags}" CACHE STRING "" FORCE)
            endif()
        endforeach()
    endif()

    if(NOT ETD_ENABLE_RTTI)
        foreach(flag_var ${eventide_msvc_flag_vars})
            if(DEFINED ${flag_var})
                set(eventide_flags "${${flag_var}}")
                string(REGEX REPLACE "(^| )[/-]GR( |$)" " " eventide_flags "${eventide_flags}")
                string(REGEX REPLACE " +" " " eventide_flags "${eventide_flags}")
                string(STRIP "${eventide_flags}" eventide_flags)
                set(${flag_var} "${eventide_flags}" CACHE STRING "" FORCE)
            endif()
        endforeach()
    endif()
endif()

add_compile_options("-D_LIBCPP_DISABLE_AVAILABILITY")

set(eventide_primary_sanitizers "")
if(ETD_SANITIZE_ADDRESS)
    list(APPEND eventide_primary_sanitizers "address")
endif()
if(ETD_SANITIZE_MEMORY)
    list(APPEND eventide_primary_sanitizers "memory")
endif()
if(ETD_SANITIZE_THREAD)
    list(APPEND eventide_primary_sanitizers "thread")
endif()
list(LENGTH eventide_primary_sanitizers eventide_primary_sanitizer_count)
if(eventide_primary_sanitizer_count GREATER 1)
    message(FATAL_ERROR "ASan, MSan, and TSan are mutually exclusive.")
endif()

if(ETD_SANITIZE_MEMORY)
    if(WIN32)
        message(FATAL_ERROR "MemorySanitizer is not supported on Windows.")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "MemorySanitizer requires Clang or AppleClang.")
    endif()
endif()

if(ETD_SANITIZE_THREAD)
    if(WIN32)
        message(FATAL_ERROR "ThreadSanitizer is not supported on Windows.")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "ThreadSanitizer requires Clang, AppleClang, or GCC.")
    endif()
endif()

if(ETD_SANITIZE_UNDEFINED)
    if(WIN32)
        message(FATAL_ERROR "UndefinedBehaviorSanitizer is not supported on Windows.")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "UndefinedBehaviorSanitizer requires Clang, AppleClang, or GCC.")
    endif()
endif()

if(ETD_SANITIZE_ADDRESS)
    if(MSVC)
        set(eventide_sanitizer_compile_options "/fsanitize=address")
        set(eventide_sanitizer_link_options "")

        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            execute_process(
                COMMAND ${CMAKE_CXX_COMPILER} --print-resource-dir
                OUTPUT_VARIABLE eventide_clang_resource_dir
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )

            set(eventide_asan_lib_path "${eventide_clang_resource_dir}/lib/windows")
            list(APPEND eventide_sanitizer_link_options
                "/libpath:${eventide_asan_lib_path}"
                "clang_rt.asan_dynamic-x86_64.lib"
                "/wholearchive:clang_rt.asan_dynamic_runtime_thunk-x86_64.lib"
            )
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set(eventide_sanitizer_compile_options "-fsanitize=address")
        set(eventide_sanitizer_link_options "-fsanitize=address")
    else()
        message(FATAL_ERROR "AddressSanitizer is not supported by ${CMAKE_CXX_COMPILER_ID}.")
    endif()
elseif(ETD_SANITIZE_MEMORY)
    set(eventide_sanitizer_compile_options "-fsanitize=memory" "-fsanitize-memory-track-origins=2")
    set(eventide_sanitizer_link_options "-fsanitize=memory" "-fsanitize-memory-track-origins=2")
elseif(ETD_SANITIZE_THREAD)
    set(eventide_sanitizer_compile_options "-fsanitize=thread")
    set(eventide_sanitizer_link_options "-fsanitize=thread")
else()
    set(eventide_sanitizer_compile_options "")
    set(eventide_sanitizer_link_options "")
endif()

if(ETD_SANITIZE_UNDEFINED)
    list(APPEND eventide_sanitizer_compile_options "-fsanitize=undefined")
    list(APPEND eventide_sanitizer_link_options "-fsanitize=undefined")
endif()

if(eventide_sanitizer_compile_options)
    if(NOT MSVC)
        list(APPEND eventide_sanitizer_compile_options "-g" "-fno-omit-frame-pointer")
    endif()
    add_compile_options(${eventide_sanitizer_compile_options})
    add_link_options(${eventide_sanitizer_link_options})
endif()

if(ETD_ENABLE_CCACHE)
    if(WIN32)
        set(eventide_compiler_cache_program_name sccache)
    else()
        set(eventide_compiler_cache_program_name ccache)
    endif()

    find_program(ETD_COMPILER_CACHE_PROGRAM ${eventide_compiler_cache_program_name})
    if(ETD_COMPILER_CACHE_PROGRAM)
        if(NOT DEFINED CMAKE_C_COMPILER_LAUNCHER)
            set(CMAKE_C_COMPILER_LAUNCHER "${ETD_COMPILER_CACHE_PROGRAM}" CACHE STRING "" FORCE)
        endif()
        if(NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER)
            set(CMAKE_CXX_COMPILER_LAUNCHER "${ETD_COMPILER_CACHE_PROGRAM}" CACHE STRING "" FORCE)
        endif()
    else()
        message(STATUS
            "ETD_ENABLE_CCACHE=ON, but ${eventide_compiler_cache_program_name} was not found."
        )
    endif()
endif()

include(eventide_dependencies)

add_library(eventide_project_options INTERFACE)
add_library(eventide::project_options ALIAS eventide_project_options)

if(MSVC)
    target_compile_options(eventide_project_options INTERFACE
        /utf-8
        $<$<BOOL:${ETD_ENABLE_EXCEPTIONS}>:/EHsc>
        $<$<NOT:$<BOOL:${ETD_ENABLE_EXCEPTIONS}>>:/EHs-c->
        $<$<NOT:$<BOOL:${ETD_ENABLE_EXCEPTIONS}>>:/wd4530>
        $<$<BOOL:${ETD_ENABLE_RTTI}>:/GR>
        $<$<NOT:$<BOOL:${ETD_ENABLE_RTTI}>>:/GR->
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/Zc:preprocessor>
    )
else()
    target_compile_options(eventide_project_options INTERFACE
        $<$<BOOL:${ETD_ENABLE_EXCEPTIONS}>:-fexceptions>
        $<$<NOT:$<BOOL:${ETD_ENABLE_EXCEPTIONS}>>:-fno-exceptions>
        $<$<BOOL:${ETD_ENABLE_RTTI}>:-frtti>
        $<$<NOT:$<BOOL:${ETD_ENABLE_RTTI}>>:-fno-rtti>
    )

    if(MINGW)
        target_compile_options(eventide_project_options INTERFACE -Wa,-mbig-obj)
        target_link_libraries(eventide_project_options INTERFACE stdc++exp)
        target_link_options(eventide_project_options INTERFACE
            -static-libgcc
            -static-libstdc++
            -Wl,--allow-multiple-definition
        )
    endif()
endif()

target_compile_definitions(eventide_project_options INTERFACE
    ETD_ENABLE_EXCEPTIONS=$<IF:$<BOOL:${ETD_ENABLE_EXCEPTIONS}>,1,0>
    ETD_ENABLE_RTTI=$<IF:$<BOOL:${ETD_ENABLE_RTTI}>,1,0>
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<NOT:$<BOOL:${ETD_ENABLE_EXCEPTIONS}>>>:_HAS_EXCEPTIONS=0>
)

function(eventide_apply_project_options target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "eventide_apply_project_options(${target}) requires an existing target.")
    endif()

    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        target_link_libraries(${target} INTERFACE eventide::project_options)
    elseif(target_type STREQUAL "EXECUTABLE")
        target_link_libraries(${target} PRIVATE eventide::project_options)
    else()
        target_link_libraries(${target} PUBLIC eventide::project_options)
    endif()
endfunction()

if(ETD_BUILD_ALL_TESTS)
    if(NOT ETD_ENABLE_TEST)
        message(STATUS "ETD_BUILD_ALL_TESTS=ON: enabling ETD_ENABLE_TEST")
        set(ETD_ENABLE_TEST ON CACHE BOOL "Build unit tests" FORCE)
    endif()

    if(NOT ETD_ENABLE_ASYNC)
        message(STATUS "ETD_BUILD_ALL_TESTS=ON: enabling ETD_ENABLE_ASYNC")
        set(ETD_ENABLE_ASYNC ON CACHE BOOL "Enable async eventide target and libuv dependency" FORCE)
    endif()

    if(NOT ETD_ENABLE_OPTION)
        message(STATUS "ETD_BUILD_ALL_TESTS=ON: enabling ETD_ENABLE_OPTION")
        set(ETD_ENABLE_OPTION ON CACHE BOOL "Enable option parser target" FORCE)
    endif()

    if(NOT ETD_ENABLE_DECO)
        message(STATUS "ETD_BUILD_ALL_TESTS=ON: enabling ETD_ENABLE_DECO")
        set(ETD_ENABLE_DECO ON CACHE BOOL "Enable deco parser headers and tests" FORCE)
    endif()

    if(NOT ETD_SERDE_ENABLE_SIMDJSON)
        message(STATUS "ETD_BUILD_ALL_TESTS=ON: enabling ETD_SERDE_ENABLE_SIMDJSON")
        set(ETD_SERDE_ENABLE_SIMDJSON ON CACHE BOOL "Enable simdjson dependency for eventide::serde" FORCE)
    endif()

    if(NOT ETD_SERDE_ENABLE_YYJSON)
        message(STATUS "ETD_BUILD_ALL_TESTS=ON: enabling ETD_SERDE_ENABLE_YYJSON")
        set(ETD_SERDE_ENABLE_YYJSON ON CACHE BOOL "Enable yyjson dependency for eventide::serde" FORCE)
    endif()

    if(NOT ETD_SERDE_ENABLE_FLATBUFFERS)
        message(STATUS "ETD_BUILD_ALL_TESTS=ON: enabling ETD_SERDE_ENABLE_FLATBUFFERS")
        set(ETD_SERDE_ENABLE_FLATBUFFERS ON CACHE BOOL "Enable flatbuffers dependency for eventide::serde" FORCE)
    endif()

    if(NOT ETD_SERDE_ENABLE_TOML)
        message(STATUS "ETD_BUILD_ALL_TESTS=ON: enabling ETD_SERDE_ENABLE_TOML")
        set(ETD_SERDE_ENABLE_TOML ON CACHE BOOL "Enable tomlplusplus dependency for eventide::serde" FORCE)
    endif()
endif()

if(ETD_ENABLE_TEST)
    include(CTest)

    if(NOT ETD_ENABLE_ZEST)
        message(STATUS "ETD_ENABLE_TEST=ON: enabling ETD_ENABLE_ZEST")
        set(ETD_ENABLE_ZEST ON CACHE BOOL "Build zest test framework target" FORCE)
    endif()
endif()

add_subdirectory(src/reflection)
add_subdirectory(src/serde)

if(ETD_ENABLE_OPTION)
    add_subdirectory(src/option)
endif()

if(ETD_ENABLE_DECO AND ETD_ENABLE_OPTION)
    add_subdirectory(src/deco)
endif()

if(ETD_ENABLE_ASYNC)
    add_subdirectory(src/async)
endif()

if(ETD_ENABLE_ASYNC AND ETD_SERDE_ENABLE_SIMDJSON)
    add_subdirectory(src/ipc)
endif()

if(ETD_ENABLE_ZEST AND (NOT ETD_ENABLE_TEST OR BUILD_TESTING))
    add_subdirectory(src/zest)
endif()

if(ETD_ENABLE_TEST AND BUILD_TESTING)
    add_subdirectory(tests)
elseif(ETD_ENABLE_TEST)
    message(STATUS "BUILD_TESTING=OFF: skipping tests")
endif()

if(ETD_ENABLE_EXAMPLES)
    add_subdirectory(examples)
endif()

if(ETD_ENABLE_TEST AND BUILD_TESTING AND ETD_ENABLE_ASYNC AND ETD_SERDE_ENABLE_SIMDJSON)
    add_subdirectory(tests/integration)
endif()
