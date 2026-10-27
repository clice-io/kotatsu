kota_add_module_tests(LIBS kota::sys)
# sys_probe's sigaction finds libc's through dlsym, in libdl before glibc 2.34.
kota_add_integration_tests(LIBS kota::sys ${CMAKE_DL_LIBS})
