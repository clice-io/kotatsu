add_library(eventide_jsonrpc STATIC)
add_library(eventide::jsonrpc ALIAS eventide_jsonrpc)

target_sources(eventide_jsonrpc PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/peer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/transport.cpp"
)

target_include_directories(eventide_jsonrpc PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(eventide_jsonrpc PUBLIC
    eventide::async
    eventide::serde::json
)
