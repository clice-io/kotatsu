add_library(eventide_deco INTERFACE)
add_library(eventide::deco ALIAS eventide_deco)

target_include_directories(eventide_deco INTERFACE
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(eventide_deco INTERFACE
    eventide::option
    eventide::reflection
)

eventide_apply_project_options(eventide_deco)
