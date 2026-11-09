kotatsu_add_module_tests(LIBS kotatsu::async kotatsu::zest::async)

# The thread pool sized at startup: see thread_pool_size/main.cpp.
add_executable(async_thread_pool_size thread_pool_size/main.cpp)
target_link_libraries(async_thread_pool_size PRIVATE kotatsu::async kotatsu::sys)
kotatsu_apply_project_options(async_thread_pool_size)
set_property(GLOBAL APPEND PROPERTY kotatsu_check_programs async_thread_pool_size)
add_test(NAME async_thread_pool_size COMMAND async_thread_pool_size)
set_tests_properties(async_thread_pool_size PROPERTIES LABELS system)

# A crash stack on the pool's threads: see crash_stack/main.cpp.
add_executable(async_crash_stack crash_stack/main.cpp)
target_link_libraries(async_crash_stack PRIVATE kotatsu::async kotatsu::sys)
kotatsu_apply_project_options(async_crash_stack)
set_property(GLOBAL APPEND PROPERTY kotatsu_check_programs async_crash_stack)
add_test(NAME async_crash_stack COMMAND async_crash_stack)
set_tests_properties(async_crash_stack PROPERTIES LABELS system)

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
