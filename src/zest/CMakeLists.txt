set(eventide_cpptrace_options
    CPPTRACE_DISABLE_CXX_20_MODULES ON
    CPPTRACE_BUILD_TESTING OFF
    CPPTRACE_BUILD_BENCHMARKING OFF
    CPPTRACE_BUILD_TOOLS OFF
)

if(WIN32)
    list(APPEND eventide_cpptrace_options
        CPPTRACE_GET_SYMBOLS_WITH_DBGHELP ON
        CPPTRACE_GET_SYMBOLS_WITH_LIBDWARF OFF
        CPPTRACE_GET_SYMBOLS_WITH_ADDR2LINE OFF
        CPPTRACE_GET_SYMBOLS_WITH_LIBBACKTRACE OFF
        CPPTRACE_GET_SYMBOLS_WITH_LIBDL OFF
        CPPTRACE_GET_SYMBOLS_WITH_NOTHING OFF
    )
elseif(APPLE)
    list(APPEND eventide_cpptrace_options
        CPPTRACE_GET_SYMBOLS_WITH_ADDR2LINE ON
        CPPTRACE_ADDR2LINE_SEARCH_SYSTEM_PATH ON
        CPPTRACE_GET_SYMBOLS_WITH_LIBDWARF OFF
        CPPTRACE_GET_SYMBOLS_WITH_LIBBACKTRACE OFF
        CPPTRACE_GET_SYMBOLS_WITH_LIBDL OFF
        CPPTRACE_GET_SYMBOLS_WITH_DBGHELP OFF
        CPPTRACE_GET_SYMBOLS_WITH_NOTHING OFF
    )
else()
    list(APPEND eventide_cpptrace_options
        CPPTRACE_GET_SYMBOLS_WITH_ADDR2LINE ON
        CPPTRACE_ADDR2LINE_SEARCH_SYSTEM_PATH ON
        CPPTRACE_GET_SYMBOLS_WITH_LIBDWARF OFF
        CPPTRACE_GET_SYMBOLS_WITH_LIBBACKTRACE OFF
        CPPTRACE_GET_SYMBOLS_WITH_LIBDL OFF
        CPPTRACE_GET_SYMBOLS_WITH_DBGHELP OFF
        CPPTRACE_GET_SYMBOLS_WITH_NOTHING OFF
    )
endif()

eventide_add_git_dependency(
    cpptrace
    GIT_REPOSITORY https://github.com/jeremy-rifkin/cpptrace.git
    GIT_TAG v1.0.4
    OPTIONS
        ${eventide_cpptrace_options}
)

add_library(eventide_zest STATIC)
add_library(eventide::zest ALIAS eventide_zest)

target_sources(eventide_zest PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/trace.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/runner.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/expr.cpp"
)

target_include_directories(eventide_zest PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(eventide_zest PUBLIC
    cpptrace::cpptrace
    eventide::deco
)

eventide_apply_project_options(eventide_zest)
