#pragma once

// Raw loopback sockets, for what kota::async cannot do to a connection:
// bind without listening, connect blocking, close with a reset. The loop has
// already started Winsock by the time a test makes one.

#include <cstdint>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace kota::test {

#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t invalid_socket = INVALID_SOCKET;

inline int close_socket(socket_t sock) {
    return ::closesocket(sock);
}
#else
using socket_t = int;
constexpr socket_t invalid_socket = -1;

inline int close_socket(socket_t sock) {
    return ::close(sock);
}
#endif

/// A blocking socket connected to 127.0.0.1:`port`.
inline socket_t connect_raw(int port) {
    socket_t sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(sock == invalid_socket) {
        return sock;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    if(::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close_socket(sock);
        return invalid_socket;
    }
    return sock;
}

/// A raw socket, closed on every way out.
struct RawSocket {
    socket_t fd = invalid_socket;

    RawSocket() = default;
    RawSocket(const RawSocket&) = delete;
    RawSocket& operator=(const RawSocket&) = delete;

    ~RawSocket() {
        if(fd != invalid_socket) {
            close_socket(fd);
        }
    }
};

/// Binds `sock` to a loopback port the kernel picks, and returns the port,
/// or 0 if it cannot.
inline int bind_loopback_raw(socket_t sock) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t length = sizeof(addr);
    if(::bind(sock, reinterpret_cast<sockaddr*>(&addr), length) != 0 ||
       ::getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &length) != 0) {
        return 0;
    }
    return ntohs(addr.sin_port);
}

/// Closes `sock` with a reset instead of an orderly shutdown.
inline int reset_socket(socket_t sock) {
    linger opt{};
    opt.l_onoff = 1;
    opt.l_linger = 0;
    int set = ::setsockopt(sock,
                           SOL_SOCKET,
                           SO_LINGER,
                           reinterpret_cast<const char*>(&opt),
                           static_cast<int>(sizeof(opt)));
    return set != 0 ? set : close_socket(sock);
}

}  // namespace kota::test
