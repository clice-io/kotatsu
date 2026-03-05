add_library(eventide_ipc STATIC)
add_library(eventide::ipc ALIAS eventide_ipc)

target_sources(eventide_ipc PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/peer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/transport.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/json_codec.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/bincode_codec.cpp"
)

target_include_directories(eventide_ipc PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(eventide_ipc PUBLIC
    eventide::async
    eventide::serde::json
)
