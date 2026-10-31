add_library(kotatsu_meta INTERFACE)
add_library(kotatsu::meta ALIAS kotatsu_meta)

target_include_directories(kotatsu_meta INTERFACE
    "${PROJECT_SOURCE_DIR}/include"
)

kotatsu_apply_project_options(kotatsu_meta)
