cmake_minimum_required(VERSION 3.16)

project(eventide LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

option(EVENTIDE_ENABLE_ASYNC "Enable async eventide target and libuv dependency" ON)
option(EVENTIDE_ENABLE_OPTION "Enable option parser target" ON)
option(EVENTIDE_ENABLE_DECO "Enable deco target" ON)
option(EVENTIDE_ENABLE_TEST "Build unit tests" OFF)
option(EVENTIDE_ENABLE_EXAMPLES "Build examples" OFF)
option(EVENTIDE_ENABLE_ZEST "Build zest test framework target" OFF)
option(EVENTIDE_SERDE_ENABLE_SIMDJSON "Enable simdjson dependency for eventide::serde" OFF)
option(EVENTIDE_SERDE_ENABLE_YYJSON "Enable yyjson dependency for eventide::serde" OFF)
option(EVENTIDE_SERDE_ENABLE_FLATBUFFERS "Enable flatbuffers dependency for eventide::serde" OFF)
option(EVENTIDE_SERDE_ENABLE_TOML "Enable tomlplusplus dependency for eventide::serde" OFF)
option(EVENTIDE_BUILD_ALL_TESTS "Enable all optional test suites for CI" OFF)
option(EVENTIDE_ENABLE_EXCEPTIONS "Build eventide targets with exception support" ON)
option(EVENTIDE_ENABLE_RTTI "Build eventide targets with RTTI support" ON)
option(EVENTIDE_USE_CPM_FOR_TESTS "Use CPM.cmake instead of FetchContent when tests are enabled" OFF)
option(EVENTIDE_ENABLE_CCACHE "Enable ccache for local targets when available" OFF)
option(EVENTIDE_SANITIZE_ADDRESS "Enable AddressSanitizer" OFF)
option(EVENTIDE_SANITIZE_MEMORY "Enable MemorySanitizer" OFF)
option(EVENTIDE_SANITIZE_THREAD "Enable ThreadSanitizer" OFF)
option(EVENTIDE_SANITIZE_UNDEFINED "Enable UndefinedBehaviorSanitizer" OFF)

add_compile_options("-D_LIBCPP_DISABLE_AVAILABILITY")

set(eventide_primary_sanitizers "")
if(EVENTIDE_SANITIZE_ADDRESS)
    list(APPEND eventide_primary_sanitizers "address")
endif()
if(EVENTIDE_SANITIZE_MEMORY)
    list(APPEND eventide_primary_sanitizers "memory")
endif()
if(EVENTIDE_SANITIZE_THREAD)
    list(APPEND eventide_primary_sanitizers "thread")
endif()
list(LENGTH eventide_primary_sanitizers eventide_primary_sanitizer_count)
if(eventide_primary_sanitizer_count GREATER 1)
    message(FATAL_ERROR "ASan, MSan, and TSan are mutually exclusive.")
endif()

if(EVENTIDE_SANITIZE_MEMORY)
    if(WIN32)
        message(FATAL_ERROR "MemorySanitizer is not supported on Windows.")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "MemorySanitizer requires Clang or AppleClang.")
    endif()
endif()

if(EVENTIDE_SANITIZE_THREAD)
    if(WIN32)
        message(FATAL_ERROR "ThreadSanitizer is not supported on Windows.")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "ThreadSanitizer requires Clang, AppleClang, or GCC.")
    endif()
endif()

if(EVENTIDE_SANITIZE_UNDEFINED)
    if(WIN32)
        message(FATAL_ERROR "UndefinedBehaviorSanitizer is not supported on Windows.")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "UndefinedBehaviorSanitizer requires Clang, AppleClang, or GCC.")
    endif()
endif()

if(EVENTIDE_SANITIZE_ADDRESS)
    if(MSVC)
        set(eventide_sanitizer_compile_options "/fsanitize=address")
        set(eventide_sanitizer_link_options "")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set(eventide_sanitizer_compile_options "-fsanitize=address")
        set(eventide_sanitizer_link_options "-fsanitize=address")
    else()
        message(FATAL_ERROR "AddressSanitizer is not supported by ${CMAKE_CXX_COMPILER_ID}.")
    endif()
elseif(EVENTIDE_SANITIZE_MEMORY)
    set(eventide_sanitizer_compile_options "-fsanitize=memory" "-fsanitize-memory-track-origins=2")
    set(eventide_sanitizer_link_options "-fsanitize=memory" "-fsanitize-memory-track-origins=2")
elseif(EVENTIDE_SANITIZE_THREAD)
    set(eventide_sanitizer_compile_options "-fsanitize=thread")
    set(eventide_sanitizer_link_options "-fsanitize=thread")
else()
    set(eventide_sanitizer_compile_options "")
    set(eventide_sanitizer_link_options "")
endif()

if(EVENTIDE_SANITIZE_UNDEFINED)
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

if(EVENTIDE_ENABLE_CCACHE)
    find_program(EVENTIDE_CCACHE_PROGRAM ccache)
    if(EVENTIDE_CCACHE_PROGRAM)
        if(NOT DEFINED CMAKE_C_COMPILER_LAUNCHER AND NOT CMAKE_C_COMPILER_ID STREQUAL "MSVC")
            set(CMAKE_C_COMPILER_LAUNCHER "${EVENTIDE_CCACHE_PROGRAM}" CACHE STRING "" FORCE)
        endif()
        if(NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
            set(CMAKE_CXX_COMPILER_LAUNCHER "${EVENTIDE_CCACHE_PROGRAM}" CACHE STRING "" FORCE)
        endif()
    else()
        message(STATUS "EVENTIDE_ENABLE_CCACHE=ON, but ccache was not found.")
    endif()
endif()

include(eventide_dependencies)

add_library(eventide_project_options INTERFACE)
add_library(eventide::project_options ALIAS eventide_project_options)

if(MSVC)
    target_compile_options(eventide_project_options INTERFACE
        $<$<BOOL:${EVENTIDE_ENABLE_EXCEPTIONS}>:/EHsc>
        $<$<NOT:$<BOOL:${EVENTIDE_ENABLE_EXCEPTIONS}>>:/EHs-c->
        $<$<BOOL:${EVENTIDE_ENABLE_RTTI}>:/GR>
        $<$<NOT:$<BOOL:${EVENTIDE_ENABLE_RTTI}>>:/GR->
    )
else()
    target_compile_options(eventide_project_options INTERFACE
        $<$<BOOL:${EVENTIDE_ENABLE_EXCEPTIONS}>:-fexceptions>
        $<$<NOT:$<BOOL:${EVENTIDE_ENABLE_EXCEPTIONS}>>:-fno-exceptions>
        $<$<BOOL:${EVENTIDE_ENABLE_RTTI}>:-frtti>
        $<$<NOT:$<BOOL:${EVENTIDE_ENABLE_RTTI}>>:-fno-rtti>
    )

    if(MINGW)
        target_compile_options(eventide_project_options INTERFACE -Wa,-mbig-obj)
        target_link_options(eventide_project_options INTERFACE -static-libgcc -static-libstdc++)
    endif()
endif()

target_compile_definitions(eventide_project_options INTERFACE
    EVENTIDE_ENABLE_EXCEPTIONS=$<IF:$<BOOL:${EVENTIDE_ENABLE_EXCEPTIONS}>,1,0>
    EVENTIDE_ENABLE_RTTI=$<IF:$<BOOL:${EVENTIDE_ENABLE_RTTI}>,1,0>
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

if(EVENTIDE_BUILD_ALL_TESTS)
    if(NOT EVENTIDE_ENABLE_TEST)
        message(STATUS "EVENTIDE_BUILD_ALL_TESTS=ON: enabling EVENTIDE_ENABLE_TEST")
        set(EVENTIDE_ENABLE_TEST ON CACHE BOOL "Build unit tests" FORCE)
    endif()

    if(NOT EVENTIDE_ENABLE_ASYNC)
        message(STATUS "EVENTIDE_BUILD_ALL_TESTS=ON: enabling EVENTIDE_ENABLE_ASYNC")
        set(EVENTIDE_ENABLE_ASYNC ON CACHE BOOL "Enable async eventide target and libuv dependency" FORCE)
    endif()

    if(NOT EVENTIDE_ENABLE_OPTION)
        message(STATUS "EVENTIDE_BUILD_ALL_TESTS=ON: enabling EVENTIDE_ENABLE_OPTION")
        set(EVENTIDE_ENABLE_OPTION ON CACHE BOOL "Enable option parser target" FORCE)
    endif()

    if(NOT EVENTIDE_ENABLE_DECO)
        message(STATUS "EVENTIDE_BUILD_ALL_TESTS=ON: enabling EVENTIDE_ENABLE_DECO")
        set(EVENTIDE_ENABLE_DECO ON CACHE BOOL "Enable deco parser headers and tests" FORCE)
    endif()

    if(NOT EVENTIDE_SERDE_ENABLE_SIMDJSON)
        message(STATUS "EVENTIDE_BUILD_ALL_TESTS=ON: enabling EVENTIDE_SERDE_ENABLE_SIMDJSON")
        set(EVENTIDE_SERDE_ENABLE_SIMDJSON ON CACHE BOOL "Enable simdjson dependency for eventide::serde" FORCE)
    endif()

    if(NOT EVENTIDE_SERDE_ENABLE_YYJSON)
        message(STATUS "EVENTIDE_BUILD_ALL_TESTS=ON: enabling EVENTIDE_SERDE_ENABLE_YYJSON")
        set(EVENTIDE_SERDE_ENABLE_YYJSON ON CACHE BOOL "Enable yyjson dependency for eventide::serde" FORCE)
    endif()

    if(NOT EVENTIDE_SERDE_ENABLE_FLATBUFFERS)
        message(STATUS "EVENTIDE_BUILD_ALL_TESTS=ON: enabling EVENTIDE_SERDE_ENABLE_FLATBUFFERS")
        set(EVENTIDE_SERDE_ENABLE_FLATBUFFERS ON CACHE BOOL "Enable flatbuffers dependency for eventide::serde" FORCE)
    endif()

    if(NOT EVENTIDE_SERDE_ENABLE_TOML)
        message(STATUS "EVENTIDE_BUILD_ALL_TESTS=ON: enabling EVENTIDE_SERDE_ENABLE_TOML")
        set(EVENTIDE_SERDE_ENABLE_TOML ON CACHE BOOL "Enable tomlplusplus dependency for eventide::serde" FORCE)
    endif()
endif()

if(EVENTIDE_ENABLE_TEST)
    include(CTest)

    if(NOT EVENTIDE_ENABLE_ZEST)
        message(STATUS "EVENTIDE_ENABLE_TEST=ON: enabling EVENTIDE_ENABLE_ZEST")
        set(EVENTIDE_ENABLE_ZEST ON CACHE BOOL "Build zest test framework target" FORCE)
    endif()

    if(MSVC)
        add_compile_options("/Zc:preprocessor")
    endif()
endif()

add_subdirectory(src/reflection)
add_subdirectory(src/serde)

if(EVENTIDE_ENABLE_OPTION)
    add_subdirectory(src/option)
endif()

if(EVENTIDE_ENABLE_DECO AND EVENTIDE_ENABLE_OPTION)
    add_subdirectory(src/deco)
endif()

if(EVENTIDE_ENABLE_ASYNC)
    add_subdirectory(src/async)
endif()

if(EVENTIDE_ENABLE_ASYNC AND EVENTIDE_SERDE_ENABLE_SIMDJSON)
    add_subdirectory(src/ipc)
    add_subdirectory(src/language)
endif()

if(EVENTIDE_ENABLE_ZEST)
    add_subdirectory(src/zest)
endif()

if(EVENTIDE_ENABLE_TEST)
    add_subdirectory(tests)
endif()

if(EVENTIDE_ENABLE_EXAMPLES)
    add_subdirectory(examples)
endif()
