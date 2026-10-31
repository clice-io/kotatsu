add_library(kotatsu_ipc_lsp STATIC)
add_library(kotatsu::ipc::lsp ALIAS kotatsu_ipc_lsp)

target_sources(kotatsu_ipc_lsp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/text.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/position.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/uri.cpp"
)

target_include_directories(kotatsu_ipc_lsp PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(kotatsu_ipc_lsp PUBLIC
    kotatsu::ipc
)

kotatsu_apply_project_options(kotatsu_ipc_lsp)
