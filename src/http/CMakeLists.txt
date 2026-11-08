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
    set(_kotatsu_curl_ssl_opt CURL_USE_SCHANNEL ON)
else()
    set(_kotatsu_curl_ssl_opt CURL_USE_OPENSSL ON)
endif()

kotatsu_add_git_dependency(
    curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG curl-8_23_0
    OPTIONS
        BUILD_CURL_EXE OFF
        BUILD_SHARED_LIBS OFF
        BUILD_STATIC_LIBS ON
        CURL_DISABLE_INSTALL ON
        CURL_ENABLE_EXPORT_TARGET ON
        # TLS is the only library curl takes. The others it would find are
        # the host's: on macOS Homebrew's, whose include directory may hold
        # another OpenSSL's headers than the one found, and which reach a
        # program linking the static libcurl without their directories.
        # Without libssh2, SCP and SFTP stay out too.
        CURL_USE_LIBPSL OFF
        CURL_USE_LIBSSH2 OFF
        CURL_BROTLI OFF
        CURL_ZLIB OFF
        CURL_ZSTD OFF
        ENABLE_ARES OFF
        USE_LIBIDN2 OFF
        USE_NGHTTP2 OFF
        # HTTP and HTTPS only: every other protocol curl builds is off.
        CURL_DISABLE_DICT ON
        CURL_DISABLE_FILE ON
        CURL_DISABLE_FTP ON
        CURL_DISABLE_GOPHER ON
        CURL_DISABLE_IMAP ON
        CURL_DISABLE_IPFS ON
        CURL_DISABLE_LDAP ON
        CURL_DISABLE_LDAPS ON
        CURL_DISABLE_MQTT ON
        CURL_DISABLE_POP3 ON
        CURL_DISABLE_RTSP ON
        CURL_DISABLE_SMTP ON
        CURL_DISABLE_TELNET ON
        CURL_DISABLE_TFTP ON
        ${_kotatsu_curl_ssl_opt}
)

add_library(kotatsu_http STATIC)
add_library(kotatsu::http ALIAS kotatsu_http)

target_sources(kotatsu_http PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/bound_client.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/client.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/manager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/request.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/response.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/transfer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/util.cpp"
)

target_include_directories(kotatsu_http PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

if(WIN32)
    target_compile_definitions(kotatsu_http PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

target_link_libraries(kotatsu_http PUBLIC
    kotatsu::async
    CURL::libcurl
)

if(KOTATSU_CODEC_ENABLE_SIMDJSON)
    target_link_libraries(kotatsu_http PUBLIC
        kotatsu::codec::json
    )
endif()

kotatsu_apply_project_options(kotatsu_http)
