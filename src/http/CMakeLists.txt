if(DEFINED ENV{CONDA_PREFIX})
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(OPENSSL_ROOT_DIR "$ENV{CONDA_PREFIX}" CACHE PATH "" FORCE)
    elseif(APPLE)
        execute_process(COMMAND brew --prefix openssl@3
                        OUTPUT_VARIABLE _brew_openssl OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(_brew_openssl AND EXISTS "${_brew_openssl}")
            set(OPENSSL_ROOT_DIR "${_brew_openssl}" CACHE PATH "" FORCE)
        endif()
    endif()
endif()

if(WIN32)
    set(_kota_curl_ssl_opt CURL_USE_SCHANNEL ON)
else()
    set(_kota_curl_ssl_opt CURL_USE_OPENSSL ON)
endif()

kota_add_git_dependency(
    curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG curl-8_17_0
    OPTIONS
        BUILD_CURL_EXE OFF
        BUILD_SHARED_LIBS OFF
        BUILD_STATIC_LIBS ON
        CURL_DISABLE_INSTALL ON
        CURL_ENABLE_EXPORT_TARGET ON
        CURL_USE_LIBPSL OFF
        CURL_ZLIB OFF
        CURL_ZSTD OFF
        ENABLE_ARES OFF
        HTTP_ONLY ON
        ${_kota_curl_ssl_opt}
)

add_library(kota_http STATIC)
add_library(kota::http ALIAS kota_http)

target_sources(kota_http PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/bound_client.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/client.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/execute.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/inflight_request.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/manager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/request.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/response.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/util.cpp"
)

target_include_directories(kota_http PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

if(WIN32)
    target_compile_definitions(kota_http PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

target_link_libraries(kota_http PUBLIC
    kota::async
    CURL::libcurl
)

if(KOTA_CODEC_ENABLE_SIMDJSON)
    target_link_libraries(kota_http PUBLIC
        kota::codec::json
    )
endif()

kota_apply_project_options(kota_http)
