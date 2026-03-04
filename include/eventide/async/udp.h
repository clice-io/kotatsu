#pragma once

#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "error.h"
#include "owned.h"
#include "task.h"

namespace eventide {

class event_loop;

class udp {
public:
    udp() noexcept;

    udp(const udp&) = delete;
    udp& operator=(const udp&) = delete;

    udp(udp&& other) noexcept;

    udp& operator=(udp&& other) noexcept;

    ~udp();

    struct Self;
    Self* operator->() noexcept;

    struct recv_flags {
        /// Packet is partial (truncated).
        bool partial;

        /// Packet came from a recvmmsg batch (Linux).
        bool mmsg_chunk;

        constexpr recv_flags(bool partial = false, bool mmsg_chunk = false) :
            partial(partial), mmsg_chunk(mmsg_chunk) {}
    };

    struct recv_result {
        std::string data;
        std::string addr;
        int port = 0;
        recv_flags flags;
    };

    struct endpoint {
        std::string addr;
        int port = 0;
    };

    /// Multicast membership operation.
    enum class membership {
        join,  // join multicast group
        leave  // leave multicast group
    };

    struct create_options {
        /// Restrict socket to IPv6 only (ignore IPv4-mapped addresses).
        bool ipv6_only;

        /// Enable recvmmsg batching when supported.
        bool recvmmsg;

        constexpr create_options(bool ipv6_only = false, bool recvmmsg = false) :
            ipv6_only(ipv6_only), recvmmsg(recvmmsg) {}
    };

    struct bind_options {
        /// Restrict socket to IPv6 only (ignore IPv4-mapped addresses).
        bool ipv6_only;

        /// Enable SO_REUSEADDR if supported.
        bool reuse_addr;

        /// Enable SO_REUSEPORT if supported.
        bool reuse_port;

        constexpr bind_options(bool ipv6_only = false,
                               bool reuse_addr = false,
                               bool reuse_port = false) :
            ipv6_only(ipv6_only), reuse_addr(reuse_addr), reuse_port(reuse_port) {}
    };

    static result<udp> create(event_loop& loop = event_loop::current());

    static result<udp> create(create_options options = create_options{},
                              event_loop& loop = event_loop::current());

    static result<udp> open(int fd, event_loop& loop = event_loop::current());

    error bind(std::string_view host, int port, bind_options options = bind_options{});

    error connect(std::string_view host, int port);

    error disconnect();

    task<error> send(std::span<const char> data, std::string_view host, int port);

    task<error> send(std::span<const char> data);

    error try_send(std::span<const char> data, std::string_view host, int port);

    error try_send(std::span<const char> data);

    result<endpoint> getsockname() const;

    result<endpoint> getpeername() const;

    error set_membership(std::string_view multicast_addr,
                         std::string_view interface_addr,
                         membership m);

    error set_source_membership(std::string_view multicast_addr,
                                std::string_view interface_addr,
                                std::string_view source_addr,
                                membership m);

    error set_multicast_loop(bool on);

    error set_multicast_ttl(int ttl);

    error set_multicast_interface(std::string_view interface_addr);

    error set_broadcast(bool on);

    error set_ttl(int ttl);

    bool using_recvmmsg() const;

    std::size_t send_queue_size() const;

    std::size_t send_queue_count() const;

    error stop_recv();

    task<result<recv_result>> recv();

private:
    explicit udp(unique_handle<Self> self) noexcept;

    unique_handle<Self> self;
};

}  // namespace eventide
