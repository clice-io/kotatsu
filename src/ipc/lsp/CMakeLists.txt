add_library(kota_ipc_lsp STATIC)
add_library(kota::ipc::lsp ALIAS kota_ipc_lsp)

target_sources(kota_ipc_lsp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/position.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/uri.cpp"
)

target_include_directories(kota_ipc_lsp PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(kota_ipc_lsp PUBLIC
    kota::ipc
)

kota_apply_project_options(kota_ipc_lsp)
