add_library(eventide_option STATIC)
add_library(eventide::option ALIAS eventide_option)

target_sources(eventide_option PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/opt_table.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/option.cc"
)

target_include_directories(eventide_option PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

eventide_apply_project_options(eventide_option)
