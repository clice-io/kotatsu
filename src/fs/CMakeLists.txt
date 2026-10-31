add_library(kotatsu_fs STATIC
    cygwin.cpp
    directory.cpp
    file.cpp
    mapping.cpp
    path.cpp
    resolve.cpp
    status.cpp
    temp.cpp
    volume.cpp
    windows.cpp
)
add_library(kotatsu::fs ALIAS kotatsu_fs)

target_include_directories(kotatsu_fs PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(kotatsu_fs PUBLIC
    kotatsu::support
)

kotatsu_apply_project_options(kotatsu_fs)
