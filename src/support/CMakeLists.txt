add_library(kota_support STATIC
    glob_pattern.cpp
)
add_library(kota::support ALIAS kota_support)

target_include_directories(kota_support PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

kota_apply_project_options(kota_support)
