# The LSP base protocol's framing, the messages of each codec and the blocking
# client, without the event loop: a program that only reads and writes
# messages takes neither libuv nor the DLLs it imports.
add_library(kotatsu_ipc_codec STATIC)
add_library(kotatsu::ipc::codec ALIAS kotatsu_ipc_codec)

target_sources(kotatsu_ipc_codec PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/framing.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/blocking_client.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/codec/bincode.cpp"
)

target_include_directories(kotatsu_ipc_codec PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(kotatsu_ipc_codec PUBLIC
    kotatsu::codec
    kotatsu::support
)

kotatsu_apply_project_options(kotatsu_ipc_codec)

# kotatsu::codec carries simdjson when it is enabled.
if(KOTATSU_CODEC_ENABLE_SIMDJSON)
    target_sources(kotatsu_ipc_codec PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/codec/json.cpp"
    )
endif()

# The peers, on the event loop.
if(KOTATSU_ENABLE_ASYNC)
    add_library(kotatsu_ipc STATIC)
    add_library(kotatsu::ipc ALIAS kotatsu_ipc)

    target_sources(kotatsu_ipc PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/transport.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/recording_transport.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/peer/bincode.cpp"
    )

    if(KOTATSU_CODEC_ENABLE_SIMDJSON)
        target_sources(kotatsu_ipc PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/peer/json.cpp"
        )
    endif()

    target_include_directories(kotatsu_ipc PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
    )

    target_link_libraries(kotatsu_ipc PUBLIC
        kotatsu::ipc::codec
        kotatsu::async
    )

    kotatsu_apply_project_options(kotatsu_ipc)

    add_subdirectory(lsp)
endif()
