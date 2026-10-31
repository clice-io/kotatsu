kotatsu_add_module_tests(LIBS kotatsu::sys)
# sys_probe's sigaction finds libc's through dlsym, in libdl before glibc 2.34.
kotatsu_add_integration_tests(LIBS kotatsu::sys ${CMAKE_DL_LIBS})
