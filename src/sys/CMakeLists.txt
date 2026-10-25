# A static library of one translation unit per area: a program links only the
# objects it calls into, and on Windows imports only their DLLs. catter's hook
# library, which runs in every process a build starts, takes the environment
# and program lookup without the rest.
add_library(kota_sys STATIC
    cgroup.cpp
    command_line.cpp
    console.cpp
    cpu.cpp
    cpu_cores.cpp
    dirs.cpp
    env.cpp
    exit_status.cpp
    host.cpp
    memory.cpp
    process.cpp
    program.cpp
    temp_dir.cpp
    title.cpp
    uname.cpp
)
add_library(kota::sys ALIAS kota_sys)

target_include_directories(kota_sys PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(kota_sys PUBLIC
    kota::fs
    kota::support
)

# ntdll: parent_pid, parallelism, cpu_cores, uname; advapi32: the registry of
# cpu_cores and uname; shell32 and ole32: the known folders of dirs.
if(WIN32)
    target_link_libraries(kota_sys PUBLIC advapi32 ntdll ole32 shell32)
endif()

kota_apply_project_options(kota_sys)
