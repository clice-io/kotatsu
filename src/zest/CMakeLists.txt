eventide_apply_cache_options(
    CPPTRACE_DISABLE_CXX_20_MODULES ON
)
FetchContent_Declare(
    cpptrace
    GIT_REPOSITORY https://github.com/jeremy-rifkin/cpptrace.git
    GIT_TAG v1.0.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(cpptrace)

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
)

eventide_apply_project_options(eventide_zest)
