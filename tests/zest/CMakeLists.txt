kota_add_module_tests(LIBS kota::zest kota::zest::async)

# zest's own runner, checked end to end by integration/check_runner.cmake.
add_executable(zest_runner_fixture integration/runner_fixture.cpp)
target_link_libraries(zest_runner_fixture PRIVATE kota::zest kota::zest::async)
kota_apply_project_options(zest_runner_fixture)
# On Windows the fixture links with garbage collection, which can drop the
# cases' registrations, so that check_runner.cmake sees whether zest keeps them.
# lld-link's /OPT:REF also merges identical constants by default, such as "*"
# into the tail of L"*", which ASan reports as an ODR violation.
if(MSVC)
    target_link_options(zest_runner_fixture PRIVATE /OPT:REF /OPT:NOICF)
elseif(WIN32)
    target_compile_options(zest_runner_fixture PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(zest_runner_fixture PRIVATE -Wl,--gc-sections)
endif()
