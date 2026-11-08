kotatsu_add_module_tests(LIBS kotatsu::sys)
kotatsu_add_integration_tests(LIBS kotatsu::sys)
# Debug information for llvm-symbolizer to decode stacktrace_probe's frames
# by, which MinGW's -g0 for drivers leaves out.
if(TARGET stacktrace_probe AND NOT MSVC)
    target_compile_options(stacktrace_probe PRIVATE -g)
endif()
