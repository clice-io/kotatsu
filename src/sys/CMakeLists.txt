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
    dirs.cpp
    env.cpp
    exit_status.cpp
    host.cpp
    memory.cpp
    os_version.cpp
    process.cpp
    program.cpp
    signals.cpp
    spawn_posix.cpp
    spawn_windows.cpp
    temp_dir.cpp
    title.cpp
)
add_library(kotatsu::sys ALIAS kotatsu_sys)

target_include_directories(kotatsu_sys PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

# pthread_sigmask around spawn's fork, in libc itself only from glibc 2.34.
find_package(Threads REQUIRED)

target_link_libraries(kotatsu_sys PUBLIC
    kotatsu::fs
    kotatsu::support
    Threads::Threads
)

# ntdll: parent_pid, parallelism, cpu_cores, os; advapi32: the registry of
# cpu_cores; shell32 and ole32: the known folders of dirs.
if(WIN32)
    target_link_libraries(kotatsu_sys PUBLIC advapi32 ntdll ole32 shell32)
endif()

kotatsu_apply_project_options(kotatsu_sys)
