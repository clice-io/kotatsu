#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "async/harness/loop_fixture.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

// Raw sockets, for what kota::async cannot do to a connection. The loop has
// already started Winsock by the time a test makes one.
#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t invalid_socket = INVALID_SOCKET;

int close_socket(socket_t sock) {
    return ::closesocket(sock);
}
#else
using socket_t = int;
constexpr socket_t invalid_socket = -1;

int close_socket(socket_t sock) {
    return ::close(sock);
}
#endif

/// A blocking socket connected to 127.0.0.1:`port`.
socket_t connect_raw(int port) {
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
int bind_loopback_raw(socket_t sock) {
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
int reset_socket(socket_t sock) {
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

/// A listener on a loopback port of its own.
struct Listener {
    tcp::acceptor acceptor;
    int port = 0;
};

result<Listener> listen_loopback(event_loop& loop) {
    auto acceptor = tcp::listen("127.0.0.1", 0, loop);
    if(!acceptor) {
        return outcome_error(acceptor.error());
    }
    auto name = acceptor->getsockname();
    if(!name) {
        return outcome_error(name.error());
    }
    return Listener{.acceptor = std::move(*acceptor), .port = name->port};
}

/// A task that finishes at once: when_any cancels what it races as soon as
/// that has started.
task<> finished() {
    co_return;
}

/// Everything `connection` reads until the peer's end.
task<std::string, error> read_to_end(stream& connection) {
    std::string all;
    while(true) {
        auto piece = co_await connection.read();
        if(!piece) {
            if(piece.error() != error::end_of_file) {
                co_await fail(piece.error());
            }
            co_return all;
        }
        all += *piece;
    }
}

ZEST_SUITE(async_io_stream_tcp, test::LoopFixture) {

ZEST_CASE(both_ends_write_and_read) {
    auto listener = listen_loopback(loop);
    ASSERT(listener.has_value());
    EXPECT(listener->port > 0);
    auto echo = [&]() -> task<void, error> {
        auto connection = co_await listener->acceptor.accept().or_fail();
        auto request = co_await connection.read().or_fail();
        co_await connection.write(request + "-pong").or_fail();
    };
    auto client = [&]() -> task<std::string, error> {
        auto connection = co_await tcp::connect("127.0.0.1", listener->port).or_fail();
        co_await connection.write(std::string_view("ping")).or_fail();
        co_return co_await connection.read().or_fail();
    };

    auto [served, reply] = run(echo(), client());
    EXPECT(served.has_value());
    ASSERT(reply.has_value());
    EXPECT(*reply == "ping-pong");
}

ZEST_CASE(try_write_writes_at_once) {
    auto listener = listen_loopback(loop);
    ASSERT(listener.has_value());
    auto serve = [&]() -> task<std::string, error> {
        auto connection = co_await listener->acceptor.accept().or_fail();
        co_return co_await connection.read().or_fail();
    };
    auto client = [&]() -> task<result<std::size_t>, error> {
        auto connection = co_await tcp::connect("127.0.0.1", listener->port).or_fail();
        co_return connection.try_write(std::string_view("now"));
    };

    auto [received, written] = run(serve(), client());
    ASSERT(written.has_value());
    ASSERT(written->has_value());
    EXPECT(**written == 3U);
    ASSERT(received.has_value());
    EXPECT(*received == "now");
}

ZEST_CASE(read_after_the_peer_closes_reports_eof) {
    auto listener = listen_loopback(loop);
    ASSERT(listener.has_value());
    auto serve = [&]() -> task<result<std::string>, error> {
        auto connection = co_await listener->acceptor.accept().or_fail();
        co_return co_await connection.read();
    };
    auto client = [&]() -> task<void, error> {
        co_await tcp::connect("127.0.0.1", listener->port).or_fail();
    };

    auto [received, connected] = run(serve(), client());
    EXPECT(connected.has_value());
    ASSERT(received.has_value());
    ASSERT(received->has_error());
    EXPECT(received->error() == error::end_of_file);
}

ZEST_CASE(read_after_the_peer_resets_fails) {
    auto listener = listen_loopback(loop);
    ASSERT(listener.has_value());
    auto sock = connect_raw(listener->port);
    ASSERT(sock != invalid_socket);
    ASSERT(reset_socket(sock) == 0);
    auto serve = [&]() -> task<result<std::size_t>, error> {
        auto connection = co_await listener->acceptor.accept().or_fail();
        std::array<char, 16> buffer{};
        co_return co_await connection.read_some(buffer);
    };

    auto [received] = run(serve());
    ASSERT(received.has_value());
    ASSERT(received->has_error());
    EXPECT(received->error() == error::connection_reset_by_peer);
}

// The reset arrives while the reader holds the data sent before it, and is
// reported once that is read. Windows drops the data a reset overtakes.
#ifndef _WIN32
ZEST_CASE(reset_after_data_is_reported_after_the_data) {
    auto listener = listen_loopback(loop);
    ASSERT(listener.has_value());
    auto sock = connect_raw(listener->port);
    ASSERT(sock != invalid_socket);
    ASSERT(::send(sock, "data", 4, 0) == 4);
    ASSERT(reset_socket(sock) == 0);
    auto serve = [&]() -> task<std::pair<std::string, result<std::string>>, error> {
        auto connection = co_await listener->acceptor.accept().or_fail();
        auto data = co_await connection.read().or_fail();
        // The reset arrives while the loop turns and nothing reads.
        co_await yield();
        auto after = co_await connection.read();
        co_return std::pair{std::move(data), std::move(after)};
    };

    auto [received] = run(serve());
    ASSERT(received.has_value());
    EXPECT(received->first == "data");
    ASSERT(received->second.has_error());
    EXPECT(received->second.error() == error::connection_reset_by_peer);
}
#endif

// The shutdown waits for the writes made with it; the server reads them,
// then the end, and answers on the half the client left open.
ZEST_CASE(shutdown_lets_the_peer_read_to_the_end_and_answer) {
    auto listener = listen_loopback(loop);
    ASSERT(listener.has_value());
    auto serve = [&]() -> task<std::string, error> {
        auto connection = co_await listener->acceptor.accept().or_fail();
        auto request = co_await read_to_end(connection).or_fail();
        co_await connection.write(request + "-answered").or_fail();
        co_return request;
    };
    auto client = [&]() -> task<std::string, error> {
        auto connection = co_await tcp::connect("127.0.0.1", listener->port).or_fail();
        co_await or_fail(co_await when_all(connection.write(std::string_view("first")),
                                           connection.write(std::string_view("second")),
                                           connection.shutdown()));
        co_return co_await read_to_end(connection).or_fail();
    };

    auto [served, answer] = run(serve(), client());
    ASSERT(served.has_value());
    EXPECT(*served == "firstsecond");
    ASSERT(answer.has_value());
    EXPECT(*answer == "firstsecond-answered");
}

// A second shutdown, even one made while the first is still pending, fails,
// and so does a write after them.
ZEST_CASE(write_and_shutdown_after_a_shutdown_fail) {
    auto listener = listen_loopback(loop);
    ASSERT(listener.has_value());
    auto serve = [&]() -> task<std::string, error> {
        auto connection = co_await listener->acceptor.accept().or_fail();
        co_return co_await read_to_end(connection).or_fail();
    };
    auto client = [&]() -> task<std::pair<error, error>, error> {
        auto connection = co_await tcp::connect("127.0.0.1", listener->port).or_fail();
        auto shut = co_await when_all(connection.shutdown(), connection.shutdown());
        auto written = co_await connection.write(std::string_view("late"));
        co_return std::pair{shut.has_error() ? shut.error() : error(),
                            written.has_error() ? written.error() : error()};
    };

    auto [served, failed] = run(serve(), client());
    ASSERT(served.has_value());
    EXPECT(served->empty());
    ASSERT(failed.has_value());
    EXPECT(failed->first == error::socket_is_not_connected);
    EXPECT(failed->second == error::broken_pipe);
}

ZEST_CASE(inert_acceptor_fails) {
    tcp::acceptor inert;

    auto [accepted] = run(inert.accept());
    ASSERT(accepted.has_error());
    EXPECT(accepted.error() == error::invalid_argument);
    EXPECT(inert.stop() == error::invalid_argument);
    auto name = inert.getsockname();
    ASSERT(name.has_error());
    EXPECT(name.error() == error::invalid_argument);
}

ZEST_CASE(second_accept_while_one_is_pending_fails) {
    auto listener = listen_loopback(loop);
    ASSERT(listener.has_value());
    auto second_then_connect = [&]() -> task<error> {
        auto second = co_await listener->acceptor.accept();
        auto connected = co_await tcp::connect("127.0.0.1", listener->port);
        EXPECT(connected.has_value());
        co_return second.has_error() ? second.error() : error();
    };

    auto [first, second] = run(listener->acceptor.accept(), second_then_connect());
    EXPECT(first.has_value());
    ASSERT(second.has_value());
    EXPECT(*second == error::resource_busy_or_locked);
}

// No client connects before the yield, so only the cancel can end the first
// accept.
ZEST_CASE(cancelled_accept_leaves_the_listener_usable) {
    auto listener = listen_loopback(loop);
    ASSERT(listener.has_value());
    auto cancel_then_serve = [&]() -> task<std::pair<std::size_t, std::string>, error> {
        auto first = co_await or_fail(co_await when_any(listener->acceptor.accept(), yield()));
        auto connection = co_await listener->acceptor.accept().or_fail();
        co_return std::pair{first.index(), co_await connection.read().or_fail()};
    };
    auto client = [&]() -> task<void, error> {
        co_await yield();
        auto connection = co_await tcp::connect("127.0.0.1", listener->port).or_fail();
        co_await connection.write(std::string_view("after")).or_fail();
    };

    auto [received, sent] = run(cancel_then_serve(), client());
    ASSERT(received.has_value());
    EXPECT(received->first == 1U);
    EXPECT(received->second == "after");
    EXPECT(sent.has_value());
}

// libuv supports reuse_port where SO_REUSEPORT balances the load, and
// refuses it on macOS and Windows.
ZEST_CASE(reuse_port_lets_two_listeners_share_a_port) {
    const tcp::options reuse_port{.reuse_port = true};
    auto first = tcp::listen("127.0.0.1", 0, reuse_port, loop);
#if defined(__APPLE__) || defined(_WIN32)
    ASSERT(first.has_error());
    EXPECT(first.error() == error::operation_not_supported_on_socket);
#else
    ASSERT(first.has_value());
    auto name = first->getsockname();
    ASSERT(name.has_value());
    auto second = tcp::listen("127.0.0.1", name->port, reuse_port, loop);
    EXPECT(second.has_value());
#endif
}

// Skipped where the host has no IPv6 loopback, as some containers do not.
ZEST_CASE(ipv6_only_listener_takes_ipv6_clients) {
    auto listener = tcp::listen("::1", 0, {.ipv6_only = true}, loop);
    if(!listener && (listener.error() == error::address_not_available ||
                     listener.error() == error::address_family_not_supported)) {
        zest::skip();
        return;
    }
    ASSERT(listener.has_value());
    auto name = listener->getsockname();
    ASSERT(name.has_value());
    EXPECT(name->addr == "::1");
    auto serve = [&]() -> task<std::string, error> {
        auto connection = co_await listener->accept().or_fail();
        co_return co_await connection.read().or_fail();
    };
    auto client = [&]() -> task<void, error> {
        auto connection = co_await tcp::connect("::1", name->port).or_fail();
        co_await connection.write(std::string_view("over-ipv6")).or_fail();
    };

    auto [received, sent] = run(serve(), client());
    ASSERT(received.has_value());
    EXPECT(*received == "over-ipv6");
    EXPECT(sent.has_value());
}

// libuv only makes an IPv6 socket IPv6-only; the flag on an IPv4 address is
// refused rather than ignored.
ZEST_CASE(ipv6_only_listen_on_an_ipv4_address_fails) {
    auto listener = tcp::listen("127.0.0.1", 0, {.ipv6_only = true}, loop);
    ASSERT(listener.has_error());
    EXPECT(listener.error() == error::invalid_argument);
}

ZEST_CASE(listen_on_a_port_in_use_fails) {
    auto listener = listen_loopback(loop);
    ASSERT(listener.has_value());

    auto taken = tcp::listen("127.0.0.1", listener->port, loop);
    ASSERT(taken.has_error());
    EXPECT(taken.error() == error::address_already_in_use);
}

ZEST_CASE(unparsable_host_fails) {
    auto listened = tcp::listen("not-an-address", 0, loop);
    ASSERT(listened.has_error());
    EXPECT(listened.error() == error::invalid_argument);

    auto [connected] = run(tcp::connect("not-an-address", 80, loop));
    ASSERT(connected.has_error());
    EXPECT(connected.error() == error::invalid_argument);
}

ZEST_CASE(connect_to_a_closed_port_fails) {
    int port = 0;
    {
        RawSocket bound;
        bound.fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        ASSERT(bound.fd != invalid_socket);
        port = bind_loopback_raw(bound.fd);
    }
    ASSERT(port > 0);

    auto [connected] = run(tcp::connect("127.0.0.1", port, loop));
    ASSERT(connected.has_error());
    EXPECT(connected.error() == error::connection_refused);
}

// A raw listener that nobody accepts from, its backlog of 0 taken by one
// connection: the kernel holds back any further connect, which then cannot
// finish by itself, so only the cancel ends it. Windows refuses such a
// connection instead.
#ifndef _WIN32
ZEST_CASE(connect_can_be_cancelled) {
    RawSocket listening;
    listening.fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT(listening.fd != invalid_socket);
    auto port = bind_loopback_raw(listening.fd);
    ASSERT(port > 0);
    ASSERT(::listen(listening.fd, 0) == 0);
    RawSocket queued;
    queued.fd = connect_raw(port);
    ASSERT(queued.fd != invalid_socket);
    auto cancel_at_once = [&]() -> task<std::size_t, error> {
        auto first =
            co_await or_fail(co_await when_any(tcp::connect("127.0.0.1", port, loop), finished()));
        co_return first.index();
    };

    auto [raced] = run(cancel_at_once());
    ASSERT(raced.has_value());
    EXPECT(*raced == 1U);
}
#endif

ZEST_CASE(connection_before_accept_is_kept) {
    auto listener = listen_loopback(loop);
    ASSERT(listener.has_value());
    event written;
    auto client = [&]() -> task<void, error> {
        auto connection = co_await tcp::connect("127.0.0.1", listener->port).or_fail();
        co_await connection.write(std::string_view("early")).or_fail();
        written.set();
    };
    auto serve_later = [&]() -> task<std::string, error> {
        co_await written.wait();
        auto connection = co_await listener->acceptor.accept().or_fail();
        co_return co_await connection.read().or_fail();
    };

    auto [sent, received] = run(client(), serve_later());
    EXPECT(sent.has_value());
    ASSERT(received.has_value());
    EXPECT(*received == "early");
}

#ifndef _WIN32
ZEST_CASE(guess_handle_tells_sockets_apart) {
    socket_t stream = ::socket(AF_INET, SOCK_STREAM, 0);
    socket_t datagram = ::socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT(stream != invalid_socket);
    ASSERT(datagram != invalid_socket);

    auto stream_kind = guess_handle(stream);
    auto datagram_kind = guess_handle(datagram);
    close_socket(stream);
    close_socket(datagram);
    EXPECT(stream_kind == handle_type::tcp);
    EXPECT(datagram_kind == handle_type::udp);
}

ZEST_CASE(open_wraps_a_connected_socket) {
    auto listener = listen_loopback(loop);
    ASSERT(listener.has_value());
    auto sock = connect_raw(listener->port);
    ASSERT(sock != invalid_socket);
    auto wrapped = tcp::open(sock, loop);
    ASSERT(wrapped.has_value());
    auto serve = [&]() -> task<std::string, error> {
        auto connection = co_await listener->acceptor.accept().or_fail();
        co_return co_await connection.read().or_fail();
    };
    auto client = [&]() -> task<void, error> {
        co_await wrapped->write(std::string_view("opened")).or_fail();
    };

    auto [received, sent] = run(serve(), client());
    ASSERT(received.has_value());
    EXPECT(*received == "opened");
    EXPECT(sent.has_value());
}
#endif

};  // ZEST_SUITE(async_io_stream_tcp)

}  // namespace

}  // namespace kota
