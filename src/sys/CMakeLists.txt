# A static library of one translation unit per area: a program links only the
# objects it calls into, and on Windows imports only their DLLs. catter's hook
# library, which runs in every process a build starts, takes the environment
# and program lookup without the rest.
add_library(kotatsu_sys STATIC
    cgroup.cpp
    capture.cpp
    child.cpp
    cmd.cpp
    console.cpp
    cpu.cpp
    cpu_cores.cpp
    crash.cpp
    dirs.cpp
    dwarf_cfi.cpp
    env.cpp
    exit_status.cpp
    exit_with_parent.cpp
    host.cpp
    memory.cpp
    os_version.cpp
    process.cpp
    program.cpp
    signals.cpp
    spawn_posix.cpp
    spawn_windows.cpp
    stacktrace_linux.cpp
    stacktrace_macos.cpp
    stacktrace_windows.cpp
    temp_dir.cpp
    thread.cpp
    title.cpp
    user.cpp
)
add_library(kotatsu::sys ALIAS kotatsu_sys)

target_include_directories(kotatsu_sys PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

# Thread, and pthread_sigmask around spawn's fork, in libc itself only from
# glibc 2.34.
find_package(Threads REQUIRED)

# write_stacktrace looks _dl_find_object up through dlsym, in libdl before
# glibc 2.34, and finds a module's CFI through its .eh_frame_hdr, which GCC
# links a static executable without.
target_link_libraries(kotatsu_sys PUBLIC
    kotatsu::fs
    kotatsu::support
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

# ntdll: parent_pid, parallelism, cpu_cores, os, process_stats; advapi32: the
# registry of cpu_cores, user_name; shell32 and ole32: the known folders of
# dirs.
if(WIN32)
    target_link_libraries(kotatsu_sys PUBLIC advapi32 ntdll ole32 shell32)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(kotatsu_sys INTERFACE "LINKER:--eh-frame-hdr")
endif()

kotatsu_apply_project_options(kotatsu_sys)
