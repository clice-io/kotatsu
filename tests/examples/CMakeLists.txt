# The examples' own tests: unit tests of what an example builds on, and the
# example programs run end to end. build_system is not run: it talks to a
# terminal, which a test has not.
kota_add_module_tests(LIBS kota::async)
target_include_directories(unit_tests PRIVATE "${PROJECT_SOURCE_DIR}/examples/build_system")

if(KOTA_ENABLE_EXAMPLES AND KOTA_CODEC_ENABLE_SIMDJSON)
    kota_add_integration_tests(
        PROGRAMS async_basics dump_dot jsonrpc_server jsonrpc_roundtrip spawn_worker
    )
endif()
