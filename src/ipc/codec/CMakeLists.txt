target_sources(kota_ipc PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/bincode.cpp"
)

if(KOTA_CODEC_ENABLE_SIMDJSON)
    target_sources(kota_ipc PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/json.cpp"
    )

    target_link_libraries(kota_ipc PUBLIC
        kota::codec::json
    )
endif()
