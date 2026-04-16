add_library(kota_option STATIC)
add_library(kota::option ALIAS kota_option)

target_sources(kota_option PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/opt_table.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/option.cc"
)

target_include_directories(kota_option PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

kota_apply_project_options(kota_option)
