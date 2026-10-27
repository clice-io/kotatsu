kota_add_module_tests(LIBS kota::ipc kota::zest::async)
kota_add_integration_tests(LIBS kota::ipc)

# The codecs built into a program alone, without the event loop: see
# codec_alone/main.cpp.
add_executable(ipc_codec_alone codec_alone/main.cpp)
target_link_libraries(ipc_codec_alone PRIVATE kota::ipc::codec)
kota_apply_project_options(ipc_codec_alone)
set_property(GLOBAL APPEND PROPERTY kota_check_programs ipc_codec_alone)
add_test(NAME ipc_codec_alone COMMAND ipc_codec_alone)
set_tests_properties(ipc_codec_alone PROPERTIES LABELS unit)
