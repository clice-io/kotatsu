add_library(kotatsu_cli_table STATIC)
add_library(kotatsu::cli::table ALIAS kotatsu_cli_table)

target_sources(kotatsu_cli_table PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/error.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/table/table.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/table/option.cc"
)

target_include_directories(kotatsu_cli_table PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(kotatsu_cli_table PUBLIC
    kotatsu::support
)

kotatsu_apply_project_options(kotatsu_cli_table)

add_library(kotatsu_cli STATIC)
add_library(kotatsu::cli ALIAS kotatsu_cli)

target_sources(kotatsu_cli PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/command.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/commands.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/parse.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/report.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/to_argv.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/usage.cc"
)

target_link_libraries(kotatsu_cli PUBLIC
    kotatsu::cli::table
    kotatsu::codec
    kotatsu::meta
)

target_link_libraries(kotatsu_cli PRIVATE
    kotatsu::sys
)

kotatsu_apply_project_options(kotatsu_cli)
