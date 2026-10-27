# The LSP base protocol's framing and the messages of each codec, without the
# event loop: a program that only reads and writes messages takes neither
# libuv nor the DLLs it imports.
add_library(kota_ipc_codec STATIC)
add_library(kota::ipc::codec ALIAS kota_ipc_codec)

target_sources(kota_ipc_codec PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/framing.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/codec/bincode.cpp"
)

target_include_directories(kota_ipc_codec PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(kota_ipc_codec PUBLIC
    kota::codec
    kota::support
)

kota_apply_project_options(kota_ipc_codec)

# kota::codec carries simdjson when it is enabled.
if(KOTA_CODEC_ENABLE_SIMDJSON)
    target_sources(kota_ipc_codec PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/codec/json.cpp"
    )
endif()

# The peers, on the event loop.
if(KOTA_ENABLE_ASYNC)
    add_library(kota_ipc STATIC)
    add_library(kota::ipc ALIAS kota_ipc)

    target_sources(kota_ipc PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/transport.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/recording_transport.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/peer/bincode.cpp"
    )

    if(KOTA_CODEC_ENABLE_SIMDJSON)
        target_sources(kota_ipc PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/peer/json.cpp"
        )
    endif()

    target_include_directories(kota_ipc PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
    )

    target_link_libraries(kota_ipc PUBLIC
        kota::ipc::codec
        kota::async
    )

    kota_apply_project_options(kota_ipc)

    add_subdirectory(lsp)
endif()
