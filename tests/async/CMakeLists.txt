kotatsu_add_module_tests(LIBS kotatsu::async kotatsu::zest::async)

# A relay let go while its loop winds down: see relay_release/main.cpp. The
# linker's --wrap is ELF's.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Threads REQUIRED)
    add_executable(async_relay_release relay_release/main.cpp)
    target_link_libraries(async_relay_release PRIVATE kotatsu::async Threads::Threads)
    target_link_options(async_relay_release PRIVATE "LINKER:--wrap=uv_async_send")
    kotatsu_apply_project_options(async_relay_release)
    set_property(GLOBAL APPEND PROPERTY kotatsu_check_programs async_relay_release)
    add_test(NAME async_relay_release COMMAND async_relay_release)
    set_tests_properties(async_relay_release PROPERTIES LABELS system)
endif()
