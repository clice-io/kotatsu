add_library(eventide_language STATIC)
add_library(eventide::language ALIAS eventide_language)

target_sources(eventide_language PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/position.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/uri.cpp"
)

target_include_directories(eventide_language PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(eventide_language PUBLIC
    eventide::jsonrpc
)
