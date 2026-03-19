add_executable(lsp_stub_server stub_server.cpp)
target_link_libraries(lsp_stub_server PRIVATE eventide::ipc::lsp)
eventide_apply_project_options(lsp_stub_server)
