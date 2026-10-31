add_library(kotatsu_support STATIC
    glob_pattern.cpp
)
add_library(kotatsu::support ALIAS kotatsu_support)

target_include_directories(kotatsu_support PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

kotatsu_apply_project_options(kotatsu_support)
