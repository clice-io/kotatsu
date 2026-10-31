kotatsu_add_module_tests(LIBS kotatsu::ipc kotatsu::zest::async)
kotatsu_add_integration_tests(LIBS kotatsu::ipc)
add_subdirectory(lsp)

# The codecs and the blocking client built into a program alone, without the
# event loop: see codec_alone/main.cpp. Its client talks over a pipe or a
# socket.
add_executable(ipc_codec_alone codec_alone/main.cpp)
target_link_libraries(ipc_codec_alone PRIVATE kotatsu::ipc::codec)
kotatsu_apply_project_options(ipc_codec_alone)
set_property(GLOBAL APPEND PROPERTY kotatsu_check_programs ipc_codec_alone)
add_test(NAME ipc_codec_alone COMMAND ipc_codec_alone)
set_tests_properties(ipc_codec_alone PROPERTIES LABELS system)
