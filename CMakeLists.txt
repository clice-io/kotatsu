cmake_minimum_required(VERSION 3.16)

project(kotatsu LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

option(KOTA_ENABLE_ASYNC "Enable async kotatsu target and libuv dependency" ON)
option(KOTA_ENABLE_HTTP "Enable HTTP client target and libcurl dependency" OFF)
option(KOTA_ENABLE_DECO "Enable deco target" ON)
option(KOTA_ENABLE_TEST "Build unit tests" OFF)
option(KOTA_ENABLE_EXAMPLES "Build examples" OFF)
option(KOTA_ENABLE_ZEST "Build zest test framework target" OFF)
option(KOTA_CODEC_ENABLE_SIMDJSON "Enable simdjson dependency for kota::codec" OFF)
option(KOTA_CODEC_ENABLE_FLATBUFFERS "Enable flatbuffers dependency for kota::codec" OFF)
option(KOTA_CODEC_ENABLE_TOML "Enable tomlplusplus dependency for kota::codec" OFF)
option(KOTA_BUILD_ALL_TESTS "Enable all optional test suites for CI" OFF)
option(KOTA_ENABLE_EXCEPTIONS "Build kotatsu targets with exception support" ON)
option(KOTA_ENABLE_RTTI "Build kotatsu targets with RTTI support" ON)
option(KOTA_USE_CPM_FOR_TESTS "Use CPM.cmake instead of FetchContent when tests are enabled" OFF)
option(KOTA_ENABLE_CCACHE "Enable compiler cache launchers when available" OFF)
option(KOTA_SANITIZE_ADDRESS "Enable AddressSanitizer" OFF)
option(KOTA_SANITIZE_MEMORY "Enable MemorySanitizer" OFF)
option(KOTA_SANITIZE_THREAD "Enable ThreadSanitizer" OFF)
option(KOTA_SANITIZE_UNDEFINED "Enable UndefinedBehaviorSanitizer" OFF)
option(KOTA_ENABLE_COVERAGE "Instrument kotatsu targets for clang source-based coverage" OFF)

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

    # clang-cl keeps line tables only in RelWithDebInfo: file and line in the
    # stacks of crash and sanitizer reports, for an eighth less build time than
    # full debug information. cl has no such option.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options("$<$<CONFIG:RelWithDebInfo>:-gline-tables-only>")
    endif()
endif()

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
list(LENGTH kota_primary_sanitizers kota_primary_sanitizer_count)
if(kota_primary_sanitizer_count GREATER 1)
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

if(KOTA_ENABLE_COVERAGE AND (MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
    message(FATAL_ERROR
        "KOTA_ENABLE_COVERAGE requires Clang or AppleClang with the GNU-style driver, "
        "not ${CMAKE_CXX_COMPILER}."
    )
endif()

if(KOTA_SANITIZE_ADDRESS)
    if(MSVC)
        # /fsanitize=address disables __forceinline; suppress C4714 so /WX does not trip on it.
        set(kota_sanitizer_compile_options
            "/fsanitize=address"
            "/wd4714"
        )
        set(kota_sanitizer_link_options "")

        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            # clang-cl's ASan moves locals to a fake stack to catch their use
            # after return, which the funclets of Windows exception handling
            # do not follow: a throw inside a catch handler then crashes in
            # the C++ runtime.
            list(APPEND kota_sanitizer_compile_options "-fsanitize-address-use-after-return=never")

            execute_process(
                COMMAND ${CMAKE_CXX_COMPILER} --print-resource-dir
                OUTPUT_VARIABLE kota_clang_resource_dir
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )

            set(kota_asan_lib_path "${kota_clang_resource_dir}/lib/windows")
            list(APPEND kota_sanitizer_link_options
                "/libpath:${kota_asan_lib_path}"
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
    # Undefined behaviour ends the test, which then fails with the report as its
    # output; a report the test survives would pass unseen in a worker's log.
    list(APPEND kota_sanitizer_compile_options "-fsanitize=undefined" "-fno-sanitize-recover=undefined")
    list(APPEND kota_sanitizer_link_options "-fsanitize=undefined")
endif()

if(kota_sanitizer_compile_options)
    if(NOT MSVC)
        # Outside Debug, sanitizer builds compile at -O1, as the ASan docs
        # suggest, with line tables only (-g1), which keep file and line in the
        # reports' stacks: about half the build time of -O2 -g. Bugs only -O2
        # exposes are left to the builds without sanitizers.
        list(APPEND kota_sanitizer_compile_options
            "$<IF:$<CONFIG:Debug>,-g,-g1>"
            "$<$<NOT:$<CONFIG:Debug>>:-O1>"
            "-fno-omit-frame-pointer"
        )
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

# kota::project_options carries ABI-relevant flags (exceptions, RTTI, source
# encoding, compile definitions, MinGW link flags, coverage instrumentation)
# and propagates PUBLIC so that downstream consumers link-compatibly with
# kotatsu.
add_library(kota_project_options INTERFACE)
add_library(kota::project_options ALIAS kota_project_options)

# kota::project_warnings carries diagnostic-only flags (-Wall/-Wextra/-Werror
# and MSVC equivalents). These must NOT leak to downstream consumers — their
# codebases have their own warning policies and may legitimately trigger
# warnings kotatsu treats as errors. Applied PRIVATE via
# kota_apply_project_options() below.
add_library(kota_project_warnings INTERFACE)
add_library(kota::project_warnings ALIAS kota_project_warnings)

if(MSVC)
    target_compile_options(kota_project_options INTERFACE
        /utf-8
        $<$<BOOL:${KOTA_ENABLE_EXCEPTIONS}>:/EHsc>
        $<$<NOT:$<BOOL:${KOTA_ENABLE_EXCEPTIONS}>>:/EHs-c->
        $<$<BOOL:${KOTA_ENABLE_RTTI}>:/GR>
        $<$<NOT:$<BOOL:${KOTA_ENABLE_RTTI}>>:/GR->
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/Zc:preprocessor>
    )
    target_compile_options(kota_project_warnings INTERFACE
        /W4
        /WX
        # Suppress warnings from headers in directories flagged as SYSTEM
        # (e.g. via kota_silence_third_party_target's
        # INTERFACE_SYSTEM_INCLUDE_DIRECTORIES), so /WX does not promote
        # third-party header warnings into our errors.
        /external:W0
        $<$<NOT:$<BOOL:${KOTA_ENABLE_EXCEPTIONS}>>:/wd4530>
        $<$<CXX_COMPILER_ID:MSVC>:/wd4033>
        # A local named like a member is deliberate wherever it happens;
        # GCC and Clang do not warn at -Wall -Wextra either.
        $<$<CXX_COMPILER_ID:MSVC>:/wd4458>
        $<$<CXX_COMPILER_ID:MSVC>:/wd4459>
    )
else()
    target_compile_options(kota_project_options INTERFACE
        $<$<BOOL:${KOTA_ENABLE_EXCEPTIONS}>:-fexceptions>
        $<$<NOT:$<BOOL:${KOTA_ENABLE_EXCEPTIONS}>>:-fno-exceptions>
        $<$<BOOL:${KOTA_ENABLE_RTTI}>:-frtti>
        $<$<NOT:$<BOOL:${KOTA_ENABLE_RTTI}>>:-fno-rtti>
    )
    target_compile_options(kota_project_warnings INTERFACE
        -Wall
        -Wextra
        -Werror
        $<$<CXX_COMPILER_ID:GNU>:-Wno-maybe-uninitialized>
    )

    # mingw with libstdc++ (GCC, or clang on a GNU sysroot): <print> and
    # <stacktrace> live in stdc++exp, and the runtime is linked statically.
    # A libc++ toolchain (llvm-mingw) needs none of this, so the check is
    # on the standard library in use rather than the compiler.
    if(MINGW)
        include(CheckCXXSymbolExists)
        check_cxx_symbol_exists(__GLIBCXX__ "version" KOTA_MINGW_LIBSTDCXX)
    endif()
    if(MINGW AND KOTA_MINGW_LIBSTDCXX)
        target_compile_options(kota_project_options INTERFACE -Wa,-mbig-obj)
        target_link_libraries(kota_project_options INTERFACE stdc++exp)
        target_link_options(kota_project_options INTERFACE
            -static-libgcc
            -static-libstdc++
            -Wl,--allow-multiple-definition
        )
        # libstdc++'s fs_path.o aliases its wstring::operator+=(wstring_view)
        # under GCC's older mangling of a pack ("II" for "IJ"), the alias
        # living in the function's COMDAT section. When lld keeps another
        # object's copy of that section, the alias goes with fs_path.o's, and
        # fs_path.o fails to link; it is pointed at the kept function instead.
        if(CMAKE_LINKER_TYPE STREQUAL "LLD")
            set(kota_libstdcxx_alias
                "_ZNSt7__cxx1112basic_stringIwSt11char_traitsIwESaIwEEpLISt17basic_string_viewIwS2_EEENSt9enable_ifIXsrSt6__and_IISt14is_convertibleIRKT_S7_ESt6__not_ISA_IPSC_PKS4_EESF_ISA_ISD_PKwEEEE5valueERS4_E4typeESD_"
            )
            string(REPLACE "__and_II" "__and_IJ" kota_libstdcxx_function "${kota_libstdcxx_alias}")
            target_link_options(kota_project_options INTERFACE
                "-Wl,-Xlink=-alternatename:${kota_libstdcxx_alias}=${kota_libstdcxx_function}"
            )
        endif()
    endif()
endif()

# Everything built on kotatsu is measured: its libraries, the tests, drivers
# and examples, and the header-only modules through the code that instantiates
# them. Third-party libraries are built without it; what kotatsu's code
# compiles of their headers is measured too, and left out of the report by
# scripts/coverage.ts. The link flag travels with the libraries, whose objects
# need the profile runtime.
if(KOTA_ENABLE_COVERAGE)
    target_compile_options(kota_project_options INTERFACE
        -fprofile-instr-generate
        -fcoverage-mapping
    )
    target_link_options(kota_project_options INTERFACE -fprofile-instr-generate)
endif()

target_compile_definitions(kota_project_options INTERFACE
    KOTA_ENABLE_EXCEPTIONS=$<IF:$<BOOL:${KOTA_ENABLE_EXCEPTIONS}>,1,0>
    KOTA_ENABLE_RTTI=$<IF:$<BOOL:${KOTA_ENABLE_RTTI}>,1,0>
    # WIN32, not MSVC: clang-native on Windows also uses MSVC CRT headers.
    $<$<BOOL:${WIN32}>:_CRT_SECURE_NO_WARNINGS>
    $<$<AND:$<BOOL:${MSVC}>,$<NOT:$<BOOL:${KOTA_ENABLE_EXCEPTIONS}>>>:_HAS_EXCEPTIONS=0>
)

function(kota_apply_project_options target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "kota_apply_project_options(${target}) requires an existing target.")
    endif()

    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        # INTERFACE libraries cannot declare PRIVATE deps; warnings are picked
        # up by the concrete (non-INTERFACE) targets that implement them via
        # their own kota_apply_project_options() calls.
        target_link_libraries(${target} INTERFACE kota::project_options)
    elseif(target_type STREQUAL "EXECUTABLE")
        target_link_libraries(${target} PRIVATE kota::project_options kota::project_warnings)
    else()
        target_link_libraries(${target} PUBLIC kota::project_options)
        target_link_libraries(${target} PRIVATE kota::project_warnings)
    endif()
endfunction()

if(KOTA_BUILD_ALL_TESTS)
    if(NOT KOTA_ENABLE_TEST)
        message(STATUS "KOTA_BUILD_ALL_TESTS=ON: enabling KOTA_ENABLE_TEST")
        set(KOTA_ENABLE_TEST ON CACHE BOOL "Build unit tests" FORCE)
    endif()

    if(NOT KOTA_ENABLE_ASYNC)
        message(STATUS "KOTA_BUILD_ALL_TESTS=ON: enabling KOTA_ENABLE_ASYNC")
        set(KOTA_ENABLE_ASYNC ON CACHE BOOL "Enable async kotatsu target and libuv dependency" FORCE)
    endif()

    if(NOT KOTA_ENABLE_DECO)
        message(STATUS "KOTA_BUILD_ALL_TESTS=ON: enabling KOTA_ENABLE_DECO")
        set(KOTA_ENABLE_DECO ON CACHE BOOL "Enable deco parser headers and tests" FORCE)
    endif()

    if(NOT KOTA_CODEC_ENABLE_SIMDJSON)
        message(STATUS "KOTA_BUILD_ALL_TESTS=ON: enabling KOTA_CODEC_ENABLE_SIMDJSON")
        set(KOTA_CODEC_ENABLE_SIMDJSON ON CACHE BOOL "Enable simdjson dependency for kota::codec" FORCE)
    endif()

    if(NOT KOTA_CODEC_ENABLE_FLATBUFFERS)
        message(STATUS "KOTA_BUILD_ALL_TESTS=ON: enabling KOTA_CODEC_ENABLE_FLATBUFFERS")
        set(KOTA_CODEC_ENABLE_FLATBUFFERS ON CACHE BOOL "Enable flatbuffers dependency for kota::codec" FORCE)
    endif()

    if(NOT KOTA_CODEC_ENABLE_TOML)
        message(STATUS "KOTA_BUILD_ALL_TESTS=ON: enabling KOTA_CODEC_ENABLE_TOML")
        set(KOTA_CODEC_ENABLE_TOML ON CACHE BOOL "Enable tomlplusplus dependency for kota::codec" FORCE)
    endif()

    if(NOT KOTA_ENABLE_HTTP)
        message(STATUS "KOTA_BUILD_ALL_TESTS=ON: enabling KOTA_ENABLE_HTTP")
        set(KOTA_ENABLE_HTTP ON CACHE BOOL "Enable HTTP client target and libcurl dependency" FORCE)
    endif()
endif()

if(KOTA_ENABLE_TEST)
    include(CTest)

    if(NOT KOTA_ENABLE_ZEST)
        message(STATUS "KOTA_ENABLE_TEST=ON: enabling KOTA_ENABLE_ZEST")
        set(KOTA_ENABLE_ZEST ON CACHE BOOL "Build zest test framework target" FORCE)
    endif()
endif()

if(KOTA_ENABLE_HTTP)
    if(NOT KOTA_ENABLE_ASYNC)
        message(STATUS "KOTA_ENABLE_HTTP=ON: enabling KOTA_ENABLE_ASYNC")
        set(KOTA_ENABLE_ASYNC ON CACHE BOOL "Enable async kotatsu target and libuv dependency" FORCE)
    endif()
endif()

if(KOTA_ENABLE_ZEST)
    if(NOT KOTA_ENABLE_DECO)
        message(STATUS "KOTA_ENABLE_ZEST=ON: enabling KOTA_ENABLE_DECO")
        set(KOTA_ENABLE_DECO ON CACHE BOOL "Enable deco parser headers and tests" FORCE)
    endif()

    if(NOT KOTA_ENABLE_ASYNC)
        message(STATUS "KOTA_ENABLE_ZEST=ON: enabling KOTA_ENABLE_ASYNC")
        set(KOTA_ENABLE_ASYNC ON CACHE BOOL "Enable async kotatsu target and libuv dependency" FORCE)
    endif()
endif()

add_subdirectory(src/meta)
add_subdirectory(src/support)
add_subdirectory(src/codec)

if(KOTA_ENABLE_DECO)
    add_subdirectory(src/deco)
endif()

if(KOTA_ENABLE_ASYNC)
    add_subdirectory(src/async)
endif()

if(KOTA_ENABLE_HTTP)
    add_subdirectory(src/http)
endif()

if(KOTA_ENABLE_ASYNC)
    add_subdirectory(src/ipc)
endif()

if(KOTA_ENABLE_ZEST AND (NOT KOTA_ENABLE_TEST OR BUILD_TESTING))
    add_subdirectory(src/zest)
endif()

# Before the tests, which run some of the examples.
if(KOTA_ENABLE_EXAMPLES)
    add_subdirectory(examples)
endif()

if(KOTA_ENABLE_TEST AND BUILD_TESTING)
    add_subdirectory(tests)
elseif(KOTA_ENABLE_TEST)
    message(STATUS "BUILD_TESTING=OFF: skipping tests")
endif()
