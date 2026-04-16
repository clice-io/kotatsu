add_library(kota_ipc STATIC)
add_library(kota::ipc ALIAS kota_ipc)

target_sources(kota_ipc PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/transport.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/recording_transport.cpp"
)

target_include_directories(kota_ipc PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(kota_ipc PUBLIC
    kota::async
)

kota_apply_project_options(kota_ipc)

add_subdirectory(codec)
add_subdirectory(lsp)
