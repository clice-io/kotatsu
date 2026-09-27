#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>

// uv.h brings in <windows.h> on Windows, whose min and max macros would
// break std::min and std::max in everything that includes this header.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "uv.h"
#include "kota/async/io/endpoint.h"
#include "kota/async/runtime/node.h"
#include "kota/async/vocab/error.h"

namespace kota::uv {

/// The error a libuv status or byte count carries: none for zero or more.
/// UV_ECANCELED is how libuv ends the requests of a handle being closed, an
/// abort as far as their callers can tell; a request its task cancelled
/// never gets here, as that task has already ended cancelled.
inline error status_to_error(std::int64_t status) noexcept {
    if(status >= 0) {
        return {};
    }
    return status == UV_ECANCELED ? error::operation_aborted : error(static_cast<int>(status));
}

/// A libuv buffer over `data`, cut to the first 4 GiB - 1 bytes: libuv takes
/// the length as an unsigned int.
inline uv_buf_t buffer_of(std::span<const char> data) noexcept {
    auto length = std::min<std::size_t>(data.size(), std::numeric_limits<unsigned int>::max());
    return ::uv_buf_init(const_cast<char*>(data.data()), static_cast<unsigned int>(length));
}

/// The numeric IPv6 or IPv4 address `host` names, with `port`; host names
/// are not looked up.
inline result<sockaddr_storage> resolve_addr(std::string_view host, int port) {
    std::string text(host);
    sockaddr_storage out{};
    if(::uv_ip6_addr(text.c_str(), port, reinterpret_cast<sockaddr_in6*>(&out)) == 0 ||
       ::uv_ip4_addr(text.c_str(), port, reinterpret_cast<sockaddr_in*>(&out)) == 0) {
        return out;
    }
    return outcome_error(error::invalid_argument);
}

/// The address and port `addr` holds; anything but IPv4 and IPv6, such as
/// the address of a Unix socket opened by descriptor, fails.
inline result<endpoint> endpoint_of(const sockaddr& addr) {
    char host[INET6_ADDRSTRLEN]{};
    int port = 0;
    if(addr.sa_family == AF_INET) {
        auto& in = reinterpret_cast<const sockaddr_in&>(addr);
        ::uv_ip4_name(&in, host, sizeof(host));
        port = ntohs(in.sin_port);
    } else if(addr.sa_family == AF_INET6) {
        auto& in6 = reinterpret_cast<const sockaddr_in6&>(addr);
        ::uv_ip6_name(&in6, host, sizeof(host));
        port = ntohs(in6.sin6_port);
    } else {
        return outcome_error(error::invalid_argument);
    }
    return endpoint{.addr = host, .port = port};
}

/// Completes `op` on a later turn of `loop`, after everything already
/// queued there: the one way io code resumes a task from outside a libuv
/// callback. Defined in loop.cpp.
void complete_later(uv_loop_t& loop, io_op& op);

}  // namespace kota::uv
