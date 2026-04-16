add_executable(lsp_stub_server stub_server.cpp)
target_link_libraries(lsp_stub_server PRIVATE kota::ipc::lsp)
kota_apply_project_options(lsp_stub_server)
target_compile_options(lsp_stub_server PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj>)
