add_library(eventide_deco STATIC)
add_library(eventide::deco ALIAS eventide_deco)

target_sources(eventide_deco PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/text.cc"
)

target_include_directories(eventide_deco PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(eventide_deco PUBLIC
    eventide::option
    eventide::reflection
)

eventide_apply_project_options(eventide_deco)
