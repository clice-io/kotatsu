#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "kota/async/io/endpoint.h"
#include "kota/async/io/loop.h"
#include "kota/async/runtime/task.h"
#include "kota/async/vocab/error.h"
#include "kota/async/vocab/owned.h"

namespace kota {

/// A UDP socket.
///
/// recv() starts receiving; from then on the socket receives until stop().
/// Datagrams that arrive while no recv() waits queue up for the next ones;
/// past 64 of them the newest are dropped, as the kernel drops them once its
/// buffer is full. One recv() may be pending at a time; a second fails with
/// error::resource_busy_or_locked. Cancelling a recv() only withdraws it.
/// Destroying the socket ends a pending recv() with
/// error::operation_aborted.
///
/// Sends may overlap: libuv sends them in the order they were made.
///
/// A default-constructed or moved-from socket is inert: what can fail fails
/// with error::invalid_argument.
class udp {
public:
    udp() noexcept;

    udp(const udp&) = delete;
    udp& operator=(const udp&) = delete;

    udp(udp&& other) noexcept;
    udp& operator=(udp&& other) noexcept;

    ~udp();

    struct recv_flags {
        /// The datagram was longer than the receive buffer and got cut.
        bool partial = false;

        /// The datagram came from a recvmmsg(2) batch.
        bool mmsg_chunk = false;
    };

    struct recv_result {
        std::string data;
        endpoint sender;
        recv_flags flags;
    };

    /// Multicast membership operation.
    enum class membership : std::uint8_t {
        /// Join the multicast group.
        join,
        /// Leave the multicast group.
        leave,
    };

    // The functions taking options come in pairs: a nested struct with
    // default member initializers cannot be a default argument within its
    // enclosing class.

    struct create_options {
        /// Receive through recvmmsg(2) on Linux, FreeBSD and macOS; ignored
        /// elsewhere. The receive buffer holds one datagram, so this reads
        /// one per call all the same, reported with recv_flags::mmsg_chunk.
        bool recvmmsg = false;
    };

    /// A socket bound on its first send, or by bind(). A socket is made
    /// IPv6-only when it is bound (bind_options::ipv6_only).
    static udp create(event_loop& loop = event_loop::current());

    static udp create(create_options options, event_loop& loop = event_loop::current());

    /// Wraps an existing socket descriptor.
    static result<udp> open(int fd, event_loop& loop = event_loop::current());

    struct bind_options {
        /// Restrict socket to IPv6 only (ignore IPv4-mapped addresses).
        bool ipv6_only = false;

        /// Enable SO_REUSEADDR if supported.
        bool reuse_addr = false;

        /// Enable SO_REUSEPORT if supported.
        bool reuse_port = false;
    };

    /// Binds to the numeric IPv4 or IPv6 address `host` and `port`; port 0
    /// lets the system pick one.
    error bind(std::string_view host, int port);

    error bind(std::string_view host, int port, bind_options options);

    /// Sends to `host`:`port` alone from now on, and receives from it alone.
    error connect(std::string_view host, int port);

    /// Undoes connect().
    error disconnect();

    /// Sends `data` to `host`:`port`; `data` must stay alive until the send
    /// completes. A cancelled send still goes out: libuv cannot take it
    /// back, and the task ends once it has.
    task<void, error> send(std::span<const char> data, std::string_view host, int port);

    /// Sends `data` to the connected peer, as the other send().
    task<void, error> send(std::span<const char> data);

    /// Sends `data` to `host`:`port` if it can go out at once.
    error try_send(std::span<const char> data, std::string_view host, int port);

    /// Sends `data` to the connected peer if it can go out at once.
    error try_send(std::span<const char> data);

    /// Receives the next datagram, or the next receive error.
    task<recv_result, error> recv();

    /// Stops receiving and ends a pending recv() with
    /// error::operation_aborted. What is queued stays; the next recv()
    /// starts receiving again.
    error stop();

    /// The address and port the socket is bound to.
    result<endpoint> getsockname() const;

    /// The address and port of the connected peer.
    result<endpoint> getpeername() const;

    /// Joins or leaves the multicast group `multicast_addr` on the
    /// interface with address `interface_addr` (empty: the system picks).
    error set_membership(std::string_view multicast_addr,
                         std::string_view interface_addr,
                         membership m);

    /// Joins or leaves the multicast group `multicast_addr` for what
    /// `source_addr` sends alone.
    error set_source_membership(std::string_view multicast_addr,
                                std::string_view interface_addr,
                                std::string_view source_addr,
                                membership m);

    /// Whether the socket receives its own multicast datagrams.
    error set_multicast_loop(bool on);

    /// The time to live of the multicast datagrams it sends, 1 to 255.
    error set_multicast_ttl(int ttl);

    /// The interface multicast datagrams go out on.
    error set_multicast_interface(std::string_view interface_addr);

    /// Whether the socket may send to broadcast addresses.
    error set_broadcast(bool on);

    /// The time to live of the datagrams it sends, 1 to 255.
    error set_ttl(int ttl);

    /// Whether receiving goes through recvmmsg(2).
    bool using_recvmmsg() const;

    /// Bytes waiting to be sent.
    std::size_t send_queue_size() const;

    /// Sends waiting to go out.
    std::size_t send_queue_count() const;

private:
    struct Self;

    explicit udp(unique_handle<Self> self) noexcept;

    unique_handle<Self> self;
};

}  // namespace kota
