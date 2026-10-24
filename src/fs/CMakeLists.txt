add_library(kota_fs STATIC
    cygwin.cpp
    directory.cpp
    file.cpp
    path.cpp
    resolve.cpp
    status.cpp
    windows.cpp
)
add_library(kota::fs ALIAS kota_fs)

target_include_directories(kota_fs PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(kota_fs PUBLIC
    kota::support
)

kota_apply_project_options(kota_fs)
