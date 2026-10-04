# build_system is not run: it talks to a terminal, which a test has not.
kota_add_module_tests(LIBS kota::async kota::zest::async)
target_include_directories(unit_tests PRIVATE "${PROJECT_SOURCE_DIR}/examples/build_system")
kota_add_integration_tests(PROGRAMS async_basics dump_dot)
