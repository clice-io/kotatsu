# The examples' own tests, by the module an example shows: unit tests of what
# it builds on, and the example programs run end to end.
add_subdirectory(async)
if(KOTA_CODEC_ENABLE_SIMDJSON)
    add_subdirectory(ipc)
endif()
