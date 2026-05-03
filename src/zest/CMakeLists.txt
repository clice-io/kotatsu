set(kota_cpptrace_options
    CPPTRACE_DISABLE_CXX_20_MODULES ON
    CPPTRACE_BUILD_TESTING OFF
    CPPTRACE_BUILD_BENCHMARKING OFF
    CPPTRACE_BUILD_TOOLS OFF
)

if(WIN32)
    list(APPEND kota_cpptrace_options
        CPPTRACE_GET_SYMBOLS_WITH_DBGHELP ON
        CPPTRACE_GET_SYMBOLS_WITH_LIBDWARF OFF
        CPPTRACE_GET_SYMBOLS_WITH_ADDR2LINE OFF
        CPPTRACE_GET_SYMBOLS_WITH_LIBBACKTRACE OFF
        CPPTRACE_GET_SYMBOLS_WITH_LIBDL OFF
        CPPTRACE_GET_SYMBOLS_WITH_NOTHING OFF
    )
elseif(APPLE)
    list(APPEND kota_cpptrace_options
        CPPTRACE_GET_SYMBOLS_WITH_LIBDWARF ON
        CPPTRACE_GET_SYMBOLS_WITH_ADDR2LINE OFF
        CPPTRACE_GET_SYMBOLS_WITH_LIBBACKTRACE OFF
        CPPTRACE_GET_SYMBOLS_WITH_LIBDL OFF
        CPPTRACE_GET_SYMBOLS_WITH_DBGHELP OFF
        CPPTRACE_GET_SYMBOLS_WITH_NOTHING OFF
    )
else()
    list(APPEND kota_cpptrace_options
        CPPTRACE_GET_SYMBOLS_WITH_LIBDWARF ON
        CPPTRACE_GET_SYMBOLS_WITH_ADDR2LINE OFF
        CPPTRACE_GET_SYMBOLS_WITH_LIBBACKTRACE OFF
        CPPTRACE_GET_SYMBOLS_WITH_LIBDL OFF
        CPPTRACE_GET_SYMBOLS_WITH_DBGHELP OFF
        CPPTRACE_GET_SYMBOLS_WITH_NOTHING OFF
    )
endif()

kota_add_git_dependency(
    cpptrace
    GIT_REPOSITORY https://github.com/jeremy-rifkin/cpptrace.git
    GIT_TAG v1.0.4
    OPTIONS
        ${kota_cpptrace_options}
)

add_library(kota_zest STATIC)
add_library(kota::zest ALIAS kota_zest)

target_sources(kota_zest PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/trace.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/runner.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/expr.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/snapshot.cpp"
)

target_include_directories(kota_zest PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(kota_zest PUBLIC
    cpptrace::cpptrace
    kota::deco
)

kota_apply_project_options(kota_zest)
