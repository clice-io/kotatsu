kota_add_module_tests(LIBS kota::async)

# build_system_tests exercises the scheduler of the build_system example.
target_include_directories(unit_tests PRIVATE "${PROJECT_SOURCE_DIR}/examples/build_system")
