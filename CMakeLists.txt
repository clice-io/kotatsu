cmake_minimum_required(VERSION 3.16)

project(kotatsu LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

option(KOTA_ENABLE_ASYNC "Enable async eventide target and libuv dependency" ON)
option(KOTA_ENABLE_OPTION "Enable option parser target" ON)
option(KOTA_ENABLE_DECO "Enable deco target" ON)
option(KOTA_ENABLE_TEST "Build unit tests" OFF)
option(KOTA_ENABLE_EXAMPLES "Build examples" OFF)
option(KOTA_ENABLE_ZEST "Build zest test framework target" OFF)
option(KOTA_CODEC_ENABLE_SIMDJSON "Enable simdjson dependency for kota::codec" OFF)
option(KOTA_CODEC_ENABLE_YYJSON "Enable yyjson dependency for kota::codec" OFF)
option(KOTA_CODEC_ENABLE_FLATBUFFERS "Enable flatbuffers dependency for kota::codec" OFF)
option(KOTA_CODEC_ENABLE_TOML "Enable tomlplusplus dependency for kota::codec" OFF)
option(KOTA_BUILD_ALL_TESTS "Enable all optional test suites for CI" OFF)
option(KOTA_ENABLE_EXCEPTIONS "Build eventide targets with exception support" ON)
option(KOTA_ENABLE_RTTI "Build eventide targets with RTTI support" ON)
option(KOTA_USE_CPM_FOR_TESTS "Use CPM.cmake instead of FetchContent when tests are enabled" OFF)
option(KOTA_ENABLE_CCACHE "Enable compiler cache launchers when available" OFF)
option(KOTA_SANITIZE_ADDRESS "Enable AddressSanitizer" OFF)
option(KOTA_SANITIZE_MEMORY "Enable MemorySanitizer" OFF)
option(KOTA_SANITIZE_THREAD "Enable ThreadSanitizer" OFF)
option(KOTA_SANITIZE_UNDEFINED "Enable UndefinedBehaviorSanitizer" OFF)

if(MSVC)
    if(KOTA_ENABLE_CCACHE)
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
endif()

add_compile_options("-D_LIBCPP_DISABLE_AVAILABILITY")

set(kota_primary_sanitizers "")
if(KOTA_SANITIZE_ADDRESS)
    list(APPEND kota_primary_sanitizers "address")
endif()
if(KOTA_SANITIZE_MEMORY)
    list(APPEND kota_primary_sanitizers "memory")
endif()
if(KOTA_SANITIZE_THREAD)
    list(APPEND kota_primary_sanitizers "thread")
endif()
list(LENGTH kota_primary_sanitizers eventide_primary_sanitizer_count)
if(eventide_primary_sanitizer_count GREATER 1)
    message(FATAL_ERROR "ASan, MSan, and TSan are mutually exclusive.")
endif()

if(KOTA_SANITIZE_MEMORY)
    if(WIN32)
        message(FATAL_ERROR "MemorySanitizer is not supported on Windows.")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "MemorySanitizer requires Clang or AppleClang.")
    endif()
endif()

if(KOTA_SANITIZE_THREAD)
    if(WIN32)
        message(FATAL_ERROR "ThreadSanitizer is not supported on Windows.")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "ThreadSanitizer requires Clang, AppleClang, or GCC.")
    endif()
endif()

if(KOTA_SANITIZE_UNDEFINED)
    if(WIN32)
        message(FATAL_ERROR "UndefinedBehaviorSanitizer is not supported on Windows.")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "UndefinedBehaviorSanitizer requires Clang, AppleClang, or GCC.")
    endif()
endif()

if(KOTA_SANITIZE_ADDRESS)
    if(MSVC)
        set(kota_sanitizer_compile_options "/fsanitize=address")
        set(kota_sanitizer_link_options "")

        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            execute_process(
                COMMAND ${CMAKE_CXX_COMPILER} --print-resource-dir
                OUTPUT_VARIABLE eventide_clang_resource_dir
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )

            set(eventide_asan_lib_path "${eventide_clang_resource_dir}/lib/windows")
            list(APPEND kota_sanitizer_link_options
                "/libpath:${eventide_asan_lib_path}"
                "clang_rt.asan_dynamic-x86_64.lib"
                "/wholearchive:clang_rt.asan_dynamic_runtime_thunk-x86_64.lib"
            )
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set(kota_sanitizer_compile_options "-fsanitize=address")
        set(kota_sanitizer_link_options "-fsanitize=address")
    else()
        message(FATAL_ERROR "AddressSanitizer is not supported by ${CMAKE_CXX_COMPILER_ID}.")
    endif()
elseif(KOTA_SANITIZE_MEMORY)
    set(kota_sanitizer_compile_options "-fsanitize=memory" "-fsanitize-memory-track-origins=2")
    set(kota_sanitizer_link_options "-fsanitize=memory" "-fsanitize-memory-track-origins=2")
elseif(KOTA_SANITIZE_THREAD)
    set(kota_sanitizer_compile_options "-fsanitize=thread")
    set(kota_sanitizer_link_options "-fsanitize=thread")
else()
    set(kota_sanitizer_compile_options "")
    set(kota_sanitizer_link_options "")
endif()

if(KOTA_SANITIZE_UNDEFINED)
    list(APPEND kota_sanitizer_compile_options "-fsanitize=undefined")
    list(APPEND kota_sanitizer_link_options "-fsanitize=undefined")
endif()

if(kota_sanitizer_compile_options)
    if(NOT MSVC)
        list(APPEND kota_sanitizer_compile_options "-g" "-fno-omit-frame-pointer")
    endif()
    add_compile_options(${kota_sanitizer_compile_options})
    add_link_options(${kota_sanitizer_link_options})
endif()

if(KOTA_ENABLE_CCACHE)
    if(WIN32)
        set(kota_compiler_cache_program_name sccache)
    else()
        set(kota_compiler_cache_program_name ccache)
    endif()

    find_program(KOTA_COMPILER_CACHE_PROGRAM ${kota_compiler_cache_program_name})
    if(KOTA_COMPILER_CACHE_PROGRAM)
        if(NOT DEFINED CMAKE_C_COMPILER_LAUNCHER)
            set(CMAKE_C_COMPILER_LAUNCHER "${KOTA_COMPILER_CACHE_PROGRAM}" CACHE STRING "" FORCE)
        endif()
        if(NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER)
            set(CMAKE_CXX_COMPILER_LAUNCHER "${KOTA_COMPILER_CACHE_PROGRAM}" CACHE STRING "" FORCE)
        endif()
    else()
        message(STATUS
            "KOTA_ENABLE_CCACHE=ON, but ${kota_compiler_cache_program_name} was not found."
        )
    endif()
endif()

include(kota_dependencies)

add_library(kota_project_options INTERFACE)
add_library(kota::project_options ALIAS kota_project_options)

if(MSVC)
    target_compile_options(kota_project_options INTERFACE
        /utf-8
        $<$<BOOL:${KOTA_ENABLE_EXCEPTIONS}>:/EHsc>
        $<$<NOT:$<BOOL:${KOTA_ENABLE_EXCEPTIONS}>>:/EHs-c->
        $<$<NOT:$<BOOL:${KOTA_ENABLE_EXCEPTIONS}>>:/wd4530>
        $<$<BOOL:${KOTA_ENABLE_RTTI}>:/GR>
        $<$<NOT:$<BOOL:${KOTA_ENABLE_RTTI}>>:/GR->
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/Zc:preprocessor>
    )
else()
    target_compile_options(kota_project_options INTERFACE
        $<$<BOOL:${KOTA_ENABLE_EXCEPTIONS}>:-fexceptions>
        $<$<NOT:$<BOOL:${KOTA_ENABLE_EXCEPTIONS}>>:-fno-exceptions>
        $<$<BOOL:${KOTA_ENABLE_RTTI}>:-frtti>
        $<$<NOT:$<BOOL:${KOTA_ENABLE_RTTI}>>:-fno-rtti>
    )

    if(MINGW)
        target_compile_options(kota_project_options INTERFACE -Wa,-mbig-obj)
        target_link_libraries(kota_project_options INTERFACE stdc++exp)
        target_link_options(kota_project_options INTERFACE
            -static-libgcc
            -static-libstdc++
            -Wl,--allow-multiple-definition
        )
    endif()
endif()

target_compile_definitions(kota_project_options INTERFACE
    KOTA_ENABLE_EXCEPTIONS=$<IF:$<BOOL:${KOTA_ENABLE_EXCEPTIONS}>,1,0>
    KOTA_ENABLE_RTTI=$<IF:$<BOOL:${KOTA_ENABLE_RTTI}>,1,0>
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<NOT:$<BOOL:${KOTA_ENABLE_EXCEPTIONS}>>>:_HAS_EXCEPTIONS=0>
)

function(kota_apply_project_options target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "kota_apply_project_options(${target}) requires an existing target.")
    endif()

    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        target_link_libraries(${target} INTERFACE kota::project_options)
    elseif(target_type STREQUAL "EXECUTABLE")
        target_link_libraries(${target} PRIVATE kota::project_options)
    else()
        target_link_libraries(${target} PUBLIC kota::project_options)
    endif()
endfunction()

if(KOTA_BUILD_ALL_TESTS)
    if(NOT KOTA_ENABLE_TEST)
        message(STATUS "KOTA_BUILD_ALL_TESTS=ON: enabling KOTA_ENABLE_TEST")
        set(KOTA_ENABLE_TEST ON CACHE BOOL "Build unit tests" FORCE)
    endif()

    if(NOT KOTA_ENABLE_ASYNC)
        message(STATUS "KOTA_BUILD_ALL_TESTS=ON: enabling KOTA_ENABLE_ASYNC")
        set(KOTA_ENABLE_ASYNC ON CACHE BOOL "Enable async eventide target and libuv dependency" FORCE)
    endif()

    if(NOT KOTA_ENABLE_OPTION)
        message(STATUS "KOTA_BUILD_ALL_TESTS=ON: enabling KOTA_ENABLE_OPTION")
        set(KOTA_ENABLE_OPTION ON CACHE BOOL "Enable option parser target" FORCE)
    endif()

    if(NOT KOTA_ENABLE_DECO)
        message(STATUS "KOTA_BUILD_ALL_TESTS=ON: enabling KOTA_ENABLE_DECO")
        set(KOTA_ENABLE_DECO ON CACHE BOOL "Enable deco parser headers and tests" FORCE)
    endif()

    if(NOT KOTA_CODEC_ENABLE_SIMDJSON)
        message(STATUS "KOTA_BUILD_ALL_TESTS=ON: enabling KOTA_CODEC_ENABLE_SIMDJSON")
        set(KOTA_CODEC_ENABLE_SIMDJSON ON CACHE BOOL "Enable simdjson dependency for kota::codec" FORCE)
    endif()

    if(NOT KOTA_CODEC_ENABLE_YYJSON)
        message(STATUS "KOTA_BUILD_ALL_TESTS=ON: enabling KOTA_CODEC_ENABLE_YYJSON")
        set(KOTA_CODEC_ENABLE_YYJSON ON CACHE BOOL "Enable yyjson dependency for kota::codec" FORCE)
    endif()

    if(NOT KOTA_CODEC_ENABLE_FLATBUFFERS)
        message(STATUS "KOTA_BUILD_ALL_TESTS=ON: enabling KOTA_CODEC_ENABLE_FLATBUFFERS")
        set(KOTA_CODEC_ENABLE_FLATBUFFERS ON CACHE BOOL "Enable flatbuffers dependency for kota::codec" FORCE)
    endif()

    if(NOT KOTA_CODEC_ENABLE_TOML)
        message(STATUS "KOTA_BUILD_ALL_TESTS=ON: enabling KOTA_CODEC_ENABLE_TOML")
        set(KOTA_CODEC_ENABLE_TOML ON CACHE BOOL "Enable tomlplusplus dependency for kota::codec" FORCE)
    endif()
endif()

if(KOTA_ENABLE_TEST)
    include(CTest)

    if(NOT KOTA_ENABLE_ZEST)
        message(STATUS "KOTA_ENABLE_TEST=ON: enabling KOTA_ENABLE_ZEST")
        set(KOTA_ENABLE_ZEST ON CACHE BOOL "Build zest test framework target" FORCE)
    endif()
endif()

if(KOTA_ENABLE_ZEST)
    if(NOT KOTA_ENABLE_OPTION)
        message(STATUS "KOTA_ENABLE_ZEST=ON: enabling KOTA_ENABLE_OPTION")
        set(KOTA_ENABLE_OPTION ON CACHE BOOL "Enable option parser target" FORCE)
    endif()

    if(NOT KOTA_ENABLE_DECO)
        message(STATUS "KOTA_ENABLE_ZEST=ON: enabling KOTA_ENABLE_DECO")
        set(KOTA_ENABLE_DECO ON CACHE BOOL "Enable deco parser headers and tests" FORCE)
    endif()
endif()

add_subdirectory(src/reflection)
add_subdirectory(src/serde)

if(KOTA_ENABLE_OPTION)
    add_subdirectory(src/option)
endif()

if(KOTA_ENABLE_DECO AND KOTA_ENABLE_OPTION)
    add_subdirectory(src/deco)
endif()

if(KOTA_ENABLE_ASYNC)
    add_subdirectory(src/async)
endif()

if(KOTA_ENABLE_ASYNC)
    add_subdirectory(src/ipc)
endif()

if(KOTA_ENABLE_ZEST AND (NOT KOTA_ENABLE_TEST OR BUILD_TESTING))
    add_subdirectory(src/zest)
endif()

if(KOTA_ENABLE_TEST AND BUILD_TESTING)
    add_subdirectory(tests)
elseif(KOTA_ENABLE_TEST)
    message(STATUS "BUILD_TESTING=OFF: skipping tests")
endif()

if(KOTA_ENABLE_EXAMPLES)
    add_subdirectory(examples)
endif()

if(KOTA_ENABLE_TEST AND BUILD_TESTING AND KOTA_ENABLE_ASYNC AND KOTA_CODEC_ENABLE_SIMDJSON)
    add_subdirectory(tests/integration)
endif()
