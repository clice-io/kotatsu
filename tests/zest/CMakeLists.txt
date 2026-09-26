kota_add_module_tests(LIBS kota::zest)

# zest's own runner, checked end to end by integration/check_runner.cmake.
add_executable(zest_runner_fixture integration/runner_fixture.cpp)
target_link_libraries(zest_runner_fixture PRIVATE kota::zest)
kota_apply_project_options(zest_runner_fixture)
