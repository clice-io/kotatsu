cmake_minimum_required(VERSION 3.16)

project(kotatsu LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

option(KOTATSU_ENABLE_ASYNC "Enable async kotatsu target and libuv dependency" ON)
option(KOTATSU_ENABLE_HTTP "Enable HTTP client target and libcurl dependency" OFF)
option(KOTATSU_ENABLE_CLI "Enable the cli target" ON)
option(KOTATSU_ENABLE_TEST "Build unit tests" OFF)
option(KOTATSU_ENABLE_EXAMPLES "Build examples" OFF)
option(KOTATSU_ENABLE_ZEST "Build zest test framework target" OFF)
option(KOTATSU_CODEC_ENABLE_SIMDJSON "Enable simdjson dependency for kotatsu::codec" OFF)
option(KOTATSU_CODEC_ENABLE_FLATBUFFERS "Enable flatbuffers dependency for kotatsu::codec" OFF)
option(KOTATSU_CODEC_ENABLE_TOML "Enable tomlplusplus dependency for kotatsu::codec" OFF)
option(KOTATSU_BUILD_ALL_TESTS "Enable all optional test suites for CI" OFF)
option(KOTATSU_ENABLE_EXCEPTIONS "Build kotatsu targets with exception support" ON)
option(KOTATSU_ENABLE_RTTI "Build kotatsu targets with RTTI support" ON)
option(KOTATSU_USE_CPM_FOR_TESTS "Use CPM.cmake instead of FetchContent when tests are enabled" OFF)
option(KOTATSU_ENABLE_CCACHE "Enable compiler cache launchers when available" OFF)
option(KOTATSU_SANITIZE_ADDRESS "Enable AddressSanitizer" OFF)
option(KOTATSU_SANITIZE_MEMORY "Enable MemorySanitizer" OFF)
option(KOTATSU_SANITIZE_THREAD "Enable ThreadSanitizer" OFF)
option(KOTATSU_SANITIZE_UNDEFINED "Enable UndefinedBehaviorSanitizer" OFF)
option(KOTATSU_ENABLE_COVERAGE "Instrument kotatsu targets for clang source-based coverage" OFF)

if(MSVC)
    if(KOTATSU_ENABLE_CCACHE)
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

set(kotatsu_primary_sanitizers "")
if(KOTATSU_SANITIZE_ADDRESS)
    list(APPEND kotatsu_primary_sanitizers "address")
endif()
if(KOTATSU_SANITIZE_MEMORY)
    list(APPEND kotatsu_primary_sanitizers "memory")
endif()
if(KOTATSU_SANITIZE_THREAD)
    list(APPEND kotatsu_primary_sanitizers "thread")
endif()
list(LENGTH kotatsu_primary_sanitizers kotatsu_primary_sanitizer_count)
if(kotatsu_primary_sanitizer_count GREATER 1)
    message(FATAL_ERROR "ASan, MSan, and TSan are mutually exclusive.")
endif()

if(KOTATSU_SANITIZE_MEMORY)
    if(WIN32)
        message(FATAL_ERROR "MemorySanitizer is not supported on Windows.")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "MemorySanitizer requires Clang or AppleClang.")
    endif()
endif()

if(KOTATSU_SANITIZE_THREAD)
    if(WIN32)
        message(FATAL_ERROR "ThreadSanitizer is not supported on Windows.")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "ThreadSanitizer requires Clang, AppleClang, or GCC.")
    endif()
endif()

if(KOTATSU_SANITIZE_UNDEFINED)
    if(WIN32)
        message(FATAL_ERROR "UndefinedBehaviorSanitizer is not supported on Windows.")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "UndefinedBehaviorSanitizer requires Clang, AppleClang, or GCC.")
    endif()
endif()

if(KOTATSU_ENABLE_COVERAGE AND (MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
    message(FATAL_ERROR
        "KOTATSU_ENABLE_COVERAGE requires Clang or AppleClang with the GNU-style driver, "
        "not ${CMAKE_CXX_COMPILER}."
    )
endif()

if(KOTATSU_SANITIZE_ADDRESS)
    if(MSVC)
        # /fsanitize=address disables __forceinline; suppress C4714 so /WX does not trip on it.
        set(kotatsu_sanitizer_compile_options
            "/fsanitize=address"
            "/wd4714"
        )
        set(kotatsu_sanitizer_link_options "")

        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            # clang-cl's ASan moves locals to a fake stack to catch their use
            # after return, which the funclets of Windows exception handling
            # do not follow: a throw inside a catch handler then crashes in
            # the C++ runtime.
            list(APPEND kotatsu_sanitizer_compile_options "-fsanitize-address-use-after-return=never")

            execute_process(
                COMMAND ${CMAKE_CXX_COMPILER} --print-resource-dir
                OUTPUT_VARIABLE kotatsu_clang_resource_dir
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )

            set(kotatsu_asan_lib_path "${kotatsu_clang_resource_dir}/lib/windows")
            list(APPEND kotatsu_sanitizer_link_options
                "/libpath:${kotatsu_asan_lib_path}"
                "clang_rt.asan_dynamic-x86_64.lib"
                "/wholearchive:clang_rt.asan_dynamic_runtime_thunk-x86_64.lib"
            )
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set(kotatsu_sanitizer_compile_options "-fsanitize=address")
        set(kotatsu_sanitizer_link_options "-fsanitize=address")
    else()
        message(FATAL_ERROR "AddressSanitizer is not supported by ${CMAKE_CXX_COMPILER_ID}.")
    endif()
elseif(KOTATSU_SANITIZE_MEMORY)
    set(kotatsu_sanitizer_compile_options "-fsanitize=memory" "-fsanitize-memory-track-origins=2")
    set(kotatsu_sanitizer_link_options "-fsanitize=memory" "-fsanitize-memory-track-origins=2")
elseif(KOTATSU_SANITIZE_THREAD)
    set(kotatsu_sanitizer_compile_options "-fsanitize=thread")
    set(kotatsu_sanitizer_link_options "-fsanitize=thread")
else()
    set(kotatsu_sanitizer_compile_options "")
    set(kotatsu_sanitizer_link_options "")
endif()

if(KOTATSU_SANITIZE_UNDEFINED)
    # Undefined behaviour ends the test, which then fails with the report as its
    # output; a report the test survives would pass unseen in a worker's log.
    list(APPEND kotatsu_sanitizer_compile_options "-fsanitize=undefined" "-fno-sanitize-recover=undefined")
    list(APPEND kotatsu_sanitizer_link_options "-fsanitize=undefined")
endif()

if(kotatsu_sanitizer_compile_options)
    if(NOT MSVC)
        # Outside Debug, sanitizer builds compile at -O1, as the ASan docs
        # suggest, with line tables only (-g1), which keep file and line in the
        # reports' stacks: about half the build time of -O2 -g. Bugs only -O2
        # exposes are left to the builds without sanitizers.
        list(APPEND kotatsu_sanitizer_compile_options
            "$<IF:$<CONFIG:Debug>,-g,-g1>"
            "$<$<NOT:$<CONFIG:Debug>>:-O1>"
            "-fno-omit-frame-pointer"
        )
    endif()
    add_compile_options(${kotatsu_sanitizer_compile_options})
    add_link_options(${kotatsu_sanitizer_link_options})
endif()

if(KOTATSU_ENABLE_CCACHE)
    if(WIN32)
        set(kotatsu_compiler_cache_program_name sccache)
    else()
        set(kotatsu_compiler_cache_program_name ccache)
    endif()

    find_program(KOTATSU_COMPILER_CACHE_PROGRAM ${kotatsu_compiler_cache_program_name})
    if(KOTATSU_COMPILER_CACHE_PROGRAM)
        if(NOT DEFINED CMAKE_C_COMPILER_LAUNCHER)
            set(CMAKE_C_COMPILER_LAUNCHER "${KOTATSU_COMPILER_CACHE_PROGRAM}" CACHE STRING "" FORCE)
        endif()
        if(NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER)
            set(CMAKE_CXX_COMPILER_LAUNCHER "${KOTATSU_COMPILER_CACHE_PROGRAM}" CACHE STRING "" FORCE)
        endif()
    else()
        message(STATUS
            "KOTATSU_ENABLE_CCACHE=ON, but ${kotatsu_compiler_cache_program_name} was not found."
        )
    endif()
endif()

include(kotatsu_dependencies)

# kotatsu::project_options carries ABI-relevant flags (exceptions, RTTI, source
# encoding, compile definitions, MinGW link flags, coverage instrumentation)
# and propagates PUBLIC so that downstream consumers link-compatibly with
# kotatsu.
add_library(kotatsu_project_options INTERFACE)
add_library(kotatsu::project_options ALIAS kotatsu_project_options)

# kotatsu::project_warnings carries diagnostic-only flags (-Wall/-Wextra/-Werror
# and MSVC equivalents). These must NOT leak to downstream consumers — their
# codebases have their own warning policies and may legitimately trigger
# warnings kotatsu treats as errors. Applied PRIVATE via
# kotatsu_apply_project_options() below.
add_library(kotatsu_project_warnings INTERFACE)
add_library(kotatsu::project_warnings ALIAS kotatsu_project_warnings)

if(MSVC)
    target_compile_options(kotatsu_project_options INTERFACE
        /utf-8
        $<$<BOOL:${KOTATSU_ENABLE_EXCEPTIONS}>:/EHsc>
        $<$<NOT:$<BOOL:${KOTATSU_ENABLE_EXCEPTIONS}>>:/EHs-c->
        $<$<BOOL:${KOTATSU_ENABLE_RTTI}>:/GR>
        $<$<NOT:$<BOOL:${KOTATSU_ENABLE_RTTI}>>:/GR->
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/Zc:preprocessor>
    )
    target_compile_options(kotatsu_project_warnings INTERFACE
        /W4
        /WX
        # Suppress warnings from headers in directories flagged as SYSTEM
        # (e.g. via kotatsu_silence_third_party_target's
        # INTERFACE_SYSTEM_INCLUDE_DIRECTORIES), so /WX does not promote
        # third-party header warnings into our errors.
        /external:W0
        $<$<NOT:$<BOOL:${KOTATSU_ENABLE_EXCEPTIONS}>>:/wd4530>
        $<$<CXX_COMPILER_ID:MSVC>:/wd4033>
        # A local named like a member is deliberate wherever it happens;
        # GCC and Clang do not warn at -Wall -Wextra either.
        $<$<CXX_COMPILER_ID:MSVC>:/wd4458>
        $<$<CXX_COMPILER_ID:MSVC>:/wd4459>
    )
else()
    target_compile_options(kotatsu_project_options INTERFACE
        $<$<BOOL:${KOTATSU_ENABLE_EXCEPTIONS}>:-fexceptions>
        $<$<NOT:$<BOOL:${KOTATSU_ENABLE_EXCEPTIONS}>>:-fno-exceptions>
        $<$<BOOL:${KOTATSU_ENABLE_RTTI}>:-frtti>
        $<$<NOT:$<BOOL:${KOTATSU_ENABLE_RTTI}>>:-fno-rtti>
    )
    target_compile_options(kotatsu_project_warnings INTERFACE
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
        check_cxx_symbol_exists(__GLIBCXX__ "version" KOTATSU_MINGW_LIBSTDCXX)
    endif()
    if(MINGW AND KOTATSU_MINGW_LIBSTDCXX)
        target_compile_options(kotatsu_project_options INTERFACE -Wa,-mbig-obj)
        target_link_libraries(kotatsu_project_options INTERFACE stdc++exp)
        target_link_options(kotatsu_project_options INTERFACE
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
            set(kotatsu_libstdcxx_alias
                "_ZNSt7__cxx1112basic_stringIwSt11char_traitsIwESaIwEEpLISt17basic_string_viewIwS2_EEENSt9enable_ifIXsrSt6__and_IISt14is_convertibleIRKT_S7_ESt6__not_ISA_IPSC_PKS4_EESF_ISA_ISD_PKwEEEE5valueERS4_E4typeESD_"
            )
            string(REPLACE "__and_II" "__and_IJ" kotatsu_libstdcxx_function "${kotatsu_libstdcxx_alias}")
            target_link_options(kotatsu_project_options INTERFACE
                "-Wl,-Xlink=-alternatename:${kotatsu_libstdcxx_alias}=${kotatsu_libstdcxx_function}"
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
if(KOTATSU_ENABLE_COVERAGE)
    target_compile_options(kotatsu_project_options INTERFACE
        -fprofile-instr-generate
        -fcoverage-mapping
    )
    target_link_options(kotatsu_project_options INTERFACE -fprofile-instr-generate)
endif()

target_compile_definitions(kotatsu_project_options INTERFACE
    KOTATSU_ENABLE_EXCEPTIONS=$<IF:$<BOOL:${KOTATSU_ENABLE_EXCEPTIONS}>,1,0>
    KOTATSU_ENABLE_RTTI=$<IF:$<BOOL:${KOTATSU_ENABLE_RTTI}>,1,0>
    # WIN32, not MSVC: clang-native on Windows also uses MSVC CRT headers.
    $<$<BOOL:${WIN32}>:_CRT_SECURE_NO_WARNINGS>
    $<$<AND:$<BOOL:${MSVC}>,$<NOT:$<BOOL:${KOTATSU_ENABLE_EXCEPTIONS}>>>:_HAS_EXCEPTIONS=0>
)

function(kotatsu_apply_project_options target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "kotatsu_apply_project_options(${target}) requires an existing target.")
    endif()

    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        # INTERFACE libraries cannot declare PRIVATE deps; warnings are picked
        # up by the concrete (non-INTERFACE) targets that implement them via
        # their own kotatsu_apply_project_options() calls.
        target_link_libraries(${target} INTERFACE kotatsu::project_options)
    elseif(target_type STREQUAL "EXECUTABLE")
        target_link_libraries(${target} PRIVATE kotatsu::project_options kotatsu::project_warnings)
    else()
        target_link_libraries(${target} PUBLIC kotatsu::project_options)
        target_link_libraries(${target} PRIVATE kotatsu::project_warnings)
    endif()
endfunction()

if(KOTATSU_BUILD_ALL_TESTS)
    if(NOT KOTATSU_ENABLE_TEST)
        message(STATUS "KOTATSU_BUILD_ALL_TESTS=ON: enabling KOTATSU_ENABLE_TEST")
        set(KOTATSU_ENABLE_TEST ON CACHE BOOL "Build unit tests" FORCE)
    endif()

    if(NOT KOTATSU_ENABLE_ASYNC)
        message(STATUS "KOTATSU_BUILD_ALL_TESTS=ON: enabling KOTATSU_ENABLE_ASYNC")
        set(KOTATSU_ENABLE_ASYNC ON CACHE BOOL "Enable async kotatsu target and libuv dependency" FORCE)
    endif()

    if(NOT KOTATSU_ENABLE_CLI)
        message(STATUS "KOTATSU_BUILD_ALL_TESTS=ON: enabling KOTATSU_ENABLE_CLI")
        set(KOTATSU_ENABLE_CLI ON CACHE BOOL "Enable the cli target" FORCE)
    endif()

    if(NOT KOTATSU_CODEC_ENABLE_SIMDJSON)
        message(STATUS "KOTATSU_BUILD_ALL_TESTS=ON: enabling KOTATSU_CODEC_ENABLE_SIMDJSON")
        set(KOTATSU_CODEC_ENABLE_SIMDJSON ON CACHE BOOL "Enable simdjson dependency for kotatsu::codec" FORCE)
    endif()

    if(NOT KOTATSU_CODEC_ENABLE_FLATBUFFERS)
        message(STATUS "KOTATSU_BUILD_ALL_TESTS=ON: enabling KOTATSU_CODEC_ENABLE_FLATBUFFERS")
        set(KOTATSU_CODEC_ENABLE_FLATBUFFERS ON CACHE BOOL "Enable flatbuffers dependency for kotatsu::codec" FORCE)
    endif()

    if(NOT KOTATSU_CODEC_ENABLE_TOML)
        message(STATUS "KOTATSU_BUILD_ALL_TESTS=ON: enabling KOTATSU_CODEC_ENABLE_TOML")
        set(KOTATSU_CODEC_ENABLE_TOML ON CACHE BOOL "Enable tomlplusplus dependency for kotatsu::codec" FORCE)
    endif()

    if(NOT KOTATSU_ENABLE_HTTP)
        message(STATUS "KOTATSU_BUILD_ALL_TESTS=ON: enabling KOTATSU_ENABLE_HTTP")
        set(KOTATSU_ENABLE_HTTP ON CACHE BOOL "Enable HTTP client target and libcurl dependency" FORCE)
    endif()
endif()

if(KOTATSU_ENABLE_TEST)
    include(CTest)

    if(NOT KOTATSU_ENABLE_ZEST)
        message(STATUS "KOTATSU_ENABLE_TEST=ON: enabling KOTATSU_ENABLE_ZEST")
        set(KOTATSU_ENABLE_ZEST ON CACHE BOOL "Build zest test framework target" FORCE)
    endif()
endif()

if(KOTATSU_ENABLE_HTTP)
    if(NOT KOTATSU_ENABLE_ASYNC)
        message(STATUS "KOTATSU_ENABLE_HTTP=ON: enabling KOTATSU_ENABLE_ASYNC")
        set(KOTATSU_ENABLE_ASYNC ON CACHE BOOL "Enable async kotatsu target and libuv dependency" FORCE)
    endif()
endif()

if(KOTATSU_ENABLE_ZEST)
    if(NOT KOTATSU_ENABLE_CLI)
        message(STATUS "KOTATSU_ENABLE_ZEST=ON: enabling KOTATSU_ENABLE_CLI")
        set(KOTATSU_ENABLE_CLI ON CACHE BOOL "Enable the cli target" FORCE)
    endif()

    if(NOT KOTATSU_ENABLE_ASYNC)
        message(STATUS "KOTATSU_ENABLE_ZEST=ON: enabling KOTATSU_ENABLE_ASYNC")
        set(KOTATSU_ENABLE_ASYNC ON CACHE BOOL "Enable async kotatsu target and libuv dependency" FORCE)
    endif()
endif()

add_subdirectory(src/meta)
add_subdirectory(src/support)
add_subdirectory(src/fs)
add_subdirectory(src/sys)
add_subdirectory(src/codec)

if(KOTATSU_ENABLE_CLI)
    add_subdirectory(src/cli)
endif()

if(KOTATSU_ENABLE_ASYNC)
    add_subdirectory(src/async)
endif()

if(KOTATSU_ENABLE_HTTP)
    add_subdirectory(src/http)
endif()

# Its codecs build without async; its peers with it.
add_subdirectory(src/ipc)

if(KOTATSU_ENABLE_ZEST AND (NOT KOTATSU_ENABLE_TEST OR BUILD_TESTING))
    add_subdirectory(src/zest)
endif()

# Before the tests, which run some of the examples.
if(KOTATSU_ENABLE_EXAMPLES)
    add_subdirectory(examples)
endif()

if(KOTATSU_ENABLE_TEST AND BUILD_TESTING)
    add_subdirectory(tests)
elseif(KOTATSU_ENABLE_TEST)
    message(STATUS "BUILD_TESTING=OFF: skipping tests")
endif()
