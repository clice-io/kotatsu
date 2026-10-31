add_library(kotatsu_deco_option STATIC)
add_library(kotatsu::deco::option ALIAS kotatsu_deco_option)
add_library(kotatsu::option ALIAS kotatsu_deco_option)

target_sources(kotatsu_deco_option PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/option/table.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/option/option.cc"
)

target_include_directories(kotatsu_deco_option PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

kotatsu_apply_project_options(kotatsu_deco_option)

add_library(kotatsu_deco STATIC)
add_library(kotatsu::deco ALIAS kotatsu_deco)

target_sources(kotatsu_deco PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/facade/runtime.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/facade/text.cc"
)

target_include_directories(kotatsu_deco PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(kotatsu_deco PUBLIC
    kotatsu::deco::option
    kotatsu::meta
)

kotatsu_apply_project_options(kotatsu_deco)
