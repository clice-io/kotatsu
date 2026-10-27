kota_add_module_tests(LIBS kota::fs)

# remove_all() with each allocation it makes failing in turn: see
# remove_all_bad_alloc/main.cpp. Built where its walk holds descriptors
# (POSIX) and an allocation can throw, but for TSan and MSan, whose runtimes
# define the operator new it replaces.
if(NOT WIN32 AND KOTA_ENABLE_EXCEPTIONS AND NOT (KOTA_SANITIZE_THREAD OR KOTA_SANITIZE_MEMORY))
    add_executable(fs_remove_all_bad_alloc remove_all_bad_alloc/main.cpp)
    target_link_libraries(fs_remove_all_bad_alloc PRIVATE kota::fs)
    kota_apply_project_options(fs_remove_all_bad_alloc)
    set_property(GLOBAL APPEND PROPERTY kota_check_programs fs_remove_all_bad_alloc)
    add_test(NAME fs_remove_all_bad_alloc COMMAND fs_remove_all_bad_alloc)
    set_tests_properties(fs_remove_all_bad_alloc PROPERTIES LABELS system)
endif()
