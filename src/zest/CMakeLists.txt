set(kotatsu_cpptrace_options
    CPPTRACE_DISABLE_CXX_20_MODULES ON
    CPPTRACE_BUILD_TESTING OFF
    CPPTRACE_BUILD_BENCHMARKING OFF
    CPPTRACE_BUILD_TOOLS OFF
)

if(WIN32)
    list(APPEND kotatsu_cpptrace_options
        CPPTRACE_GET_SYMBOLS_WITH_DBGHELP ON
        CPPTRACE_GET_SYMBOLS_WITH_LIBDWARF OFF
        CPPTRACE_GET_SYMBOLS_WITH_ADDR2LINE OFF
        CPPTRACE_GET_SYMBOLS_WITH_LIBBACKTRACE OFF
        CPPTRACE_GET_SYMBOLS_WITH_LIBDL OFF
        CPPTRACE_GET_SYMBOLS_WITH_NOTHING OFF
    )
elseif(APPLE)
    list(APPEND kotatsu_cpptrace_options
        CPPTRACE_GET_SYMBOLS_WITH_LIBDWARF ON
        CPPTRACE_GET_SYMBOLS_WITH_ADDR2LINE OFF
        CPPTRACE_GET_SYMBOLS_WITH_LIBBACKTRACE OFF
        CPPTRACE_GET_SYMBOLS_WITH_LIBDL OFF
        CPPTRACE_GET_SYMBOLS_WITH_DBGHELP OFF
        CPPTRACE_GET_SYMBOLS_WITH_NOTHING OFF
    )
else()
    list(APPEND kotatsu_cpptrace_options
        CPPTRACE_GET_SYMBOLS_WITH_LIBDWARF ON
        CPPTRACE_GET_SYMBOLS_WITH_ADDR2LINE OFF
        CPPTRACE_GET_SYMBOLS_WITH_LIBBACKTRACE OFF
        CPPTRACE_GET_SYMBOLS_WITH_LIBDL OFF
        CPPTRACE_GET_SYMBOLS_WITH_DBGHELP OFF
        CPPTRACE_GET_SYMBOLS_WITH_NOTHING OFF
    )
endif()

kotatsu_add_git_dependency(
    cpptrace
    GIT_REPOSITORY https://github.com/jeremy-rifkin/cpptrace.git
    GIT_TAG v1.0.4
    OPTIONS
        ${kotatsu_cpptrace_options}
)

add_library(kotatsu_zest STATIC)
add_library(kotatsu::zest ALIAS kotatsu_zest)

target_sources(kotatsu_zest PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/trace.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/runner.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/pool.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/worker.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/check.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/snapshot.cpp"
)

target_include_directories(kotatsu_zest PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(kotatsu_zest PUBLIC
    cpptrace::cpptrace
    kotatsu::deco
    kotatsu::support
)

# Worker processes are started and driven through kotatsu::async, with the
# arguments and the executable kotatsu::sys finds, their logs written through
# kotatsu::fs.
target_link_libraries(kotatsu_zest PRIVATE
    kotatsu::async
    kotatsu::fs
    kotatsu::sys
)

kotatsu_apply_project_options(kotatsu_zest)

# zest's support for tests of kotatsu::async code (kotatsu/zest/async.h): a target of
# its own, so that kotatsu::zest does not bring kotatsu::async to its users.
add_library(kotatsu_zest_async INTERFACE)
add_library(kotatsu::zest::async ALIAS kotatsu_zest_async)

target_link_libraries(kotatsu_zest_async INTERFACE
    kotatsu::zest
    kotatsu::async
)
