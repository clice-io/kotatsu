add_library(eventide_reflection INTERFACE)
add_library(eventide::reflection ALIAS eventide_reflection)

target_include_directories(eventide_reflection INTERFACE
    "${PROJECT_SOURCE_DIR}/include"
)

eventide_apply_project_options(eventide_reflection)
