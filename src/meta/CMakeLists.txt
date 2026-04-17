add_library(kota_meta INTERFACE)
add_library(kota::meta ALIAS kota_meta)

target_include_directories(kota_meta INTERFACE
    "${PROJECT_SOURCE_DIR}/include"
)

kota_apply_project_options(kota_meta)
