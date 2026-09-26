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
    auto acceptor = tcp::listen("127.0.0.1", 0, {}, loop);
    if(!acceptor) {
        return outcome_error(acceptor.error());
    }
    auto port = tcp::local_port(*acceptor);
    if(!port) {
        return outcome_error(port.error());
    }
    return Listener{std::move(*acceptor), *port};
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
    auto client = [&]() -> task<std::size_t, error> {
        auto connection = co_await tcp::connect("127.0.0.1", listener->port).or_fail();
        co_return co_await or_fail(connection.try_write(std::string_view("now")));
    };

    auto [received, written] = run(serve(), client());
    ASSERT(written.has_value());
    EXPECT(*written == 3U);
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

ZEST_CASE(second_accept_while_one_is_pending_fails) {
    auto listener = listen_loopback(loop);
    ASSERT(listener.has_value());
    auto first = listener->acceptor.accept();
    auto* node = first.operator->();
    auto second_then_cancel = [&]() -> task<result<tcp>> {
        auto second = co_await listener->acceptor.accept();
        node->cancel();
        co_return second;
    };

    auto [pending, second] = run(std::move(first), second_then_cancel());
    EXPECT(pending.is_cancelled());
    ASSERT(second.has_value());
    ASSERT(second->has_error());
    EXPECT(second->error() == error::connection_already_in_progress);
}

ZEST_CASE(cancelled_accept_leaves_the_listener_usable) {
    auto listener = listen_loopback(loop);
    ASSERT(listener.has_value());
    auto pending = listener->acceptor.accept();
    auto* node = pending.operator->();
    auto cancel_then_serve = [&]() -> task<std::string, error> {
        node->cancel();
        auto connection = co_await listener->acceptor.accept().or_fail();
        co_return co_await connection.read().or_fail();
    };
    auto client = [&]() -> task<void, error> {
        auto connection = co_await tcp::connect("127.0.0.1", listener->port).or_fail();
        co_await connection.write(std::string_view("after")).or_fail();
    };

    auto [cancelled, received, sent] = run(std::move(pending), cancel_then_serve(), client());
    EXPECT(cancelled.is_cancelled());
    ASSERT(received.has_value());
    EXPECT(*received == "after");
    EXPECT(sent.has_value());
}

// libuv supports reuse_port where SO_REUSEPORT balances the load, and
// refuses it on macOS and Windows.
ZEST_CASE(reuse_port_lets_two_listeners_share_a_port) {
    tcp::options reuse_port(false, true);
    auto first = tcp::listen("127.0.0.1", 0, reuse_port, loop);
#if defined(__APPLE__) || defined(_WIN32)
    ASSERT(first.has_error());
    EXPECT(first.error() == error::operation_not_supported_on_socket);
#else
    ASSERT(first.has_value());
    auto port = tcp::local_port(*first);
    ASSERT(port.has_value());
    auto second = tcp::listen("127.0.0.1", *port, reuse_port, loop);
    EXPECT(second.has_value());
#endif
}

// Skipped where the host has no IPv6 loopback, as some containers do not.
ZEST_CASE(ipv6_only_listener_takes_ipv6_clients) {
    auto listener = tcp::listen("::1", 0, tcp::options(true), loop);
    if(!listener && (listener.error() == error::address_not_available ||
                     listener.error() == error::address_family_not_supported)) {
        zest::skip();
        return;
    }
    ASSERT(listener.has_value());
    auto port = tcp::local_port(*listener);
    ASSERT(port.has_value());
    auto serve = [&]() -> task<std::string, error> {
        auto connection = co_await listener->accept().or_fail();
        co_return co_await connection.read().or_fail();
    };
    auto client = [&]() -> task<void, error> {
        auto connection = co_await tcp::connect("::1", *port).or_fail();
        co_await connection.write(std::string_view("over-ipv6")).or_fail();
    };

    auto [received, sent] = run(serve(), client());
    ASSERT(received.has_value());
    EXPECT(*received == "over-ipv6");
    EXPECT(sent.has_value());
}

ZEST_CASE(listen_on_a_port_in_use_fails) {
    auto listener = listen_loopback(loop);
    ASSERT(listener.has_value());

    auto taken = tcp::listen("127.0.0.1", listener->port, {}, loop);
    ASSERT(taken.has_error());
    EXPECT(taken.error() == error::address_already_in_use);
}

ZEST_CASE(unparsable_host_fails) {
    auto listened = tcp::listen("not-an-address", 0, {}, loop);
    ASSERT(listened.has_error());
    EXPECT(listened.error() == error::invalid_argument);

    auto [connected] = run(tcp::connect("not-an-address", 80, loop));
    ASSERT(connected.has_error());
    EXPECT(connected.error() == error::invalid_argument);
}

ZEST_CASE(connect_can_be_cancelled) {
    auto listener = listen_loopback(loop);
    ASSERT(listener.has_value());
    auto connecting = tcp::connect("127.0.0.1", listener->port, loop);
    auto* node = connecting.operator->();
    auto cancel_it = [&]() -> task<> {
        node->cancel();
        co_return;
    };

    auto [cancelled, driver] = run(std::move(connecting), cancel_it());
    EXPECT(cancelled.is_cancelled());
}

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
