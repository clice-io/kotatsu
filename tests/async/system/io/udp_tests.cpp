#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef _WIN32
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

/// A socket bound to a loopback port of its own.
struct Bound {
    udp socket;
    int port = 0;
};

result<Bound> bind_loopback(event_loop& loop, udp::create_options options = {}) {
    auto created = udp::create(options, loop);
    if(!created) {
        return outcome_error(created.error());
    }
    if(auto err = created->bind("127.0.0.1", 0)) {
        return outcome_error(err);
    }
    auto name = created->getsockname();
    if(!name) {
        return outcome_error(name.error());
    }
    return Bound{.socket = std::move(*created), .port = name->port};
}

/// A task that finishes at once: when_any cancels what it races as soon as
/// that has started.
task<> finished() {
    co_return;
}

ZEST_SUITE(async_io_udp, test::LoopFixture) {

ZEST_CASE(datagram_arrives_with_its_sender) {
    auto receiver = bind_loopback(loop);
    auto sender = bind_loopback(loop);
    ASSERT(receiver.has_value());
    ASSERT(sender.has_value());
    EXPECT(receiver->port > 0);

    auto [sent, received] =
        run(sender->socket.send(std::string_view("kotatsu-udp"), "127.0.0.1", receiver->port),
            receiver->socket.recv());
    EXPECT(sent.has_value());
    ASSERT(received.has_value());
    EXPECT(received->data == "kotatsu-udp");
    EXPECT(received->sender.addr == "127.0.0.1");
    EXPECT(received->sender.port == sender->port);
    EXPECT(!received->flags.partial);
}

ZEST_CASE(connected_socket_sends_to_its_peer) {
    auto receiver = bind_loopback(loop);
    auto sender = udp::create(loop);
    ASSERT(receiver.has_value());
    ASSERT(sender.has_value());
    ASSERT(!sender->connect("127.0.0.1", receiver->port));
    auto peer = sender->getpeername();
    ASSERT(peer.has_value());
    EXPECT(peer->port == receiver->port);

    auto [sent, received] =
        run(sender->send(std::string_view("kotatsu-connected")), receiver->socket.recv());
    EXPECT(sent.has_value());
    ASSERT(received.has_value());
    EXPECT(received->data == "kotatsu-connected");
}

ZEST_CASE(disconnect_forgets_the_peer) {
    auto receiver = bind_loopback(loop);
    auto sender = udp::create(loop);
    ASSERT(receiver.has_value());
    ASSERT(sender.has_value());
    ASSERT(!sender->connect("127.0.0.1", receiver->port));

    EXPECT(!sender->disconnect());
    auto peer = sender->getpeername();
    ASSERT(peer.has_error());
    EXPECT(peer.error() == error::socket_is_not_connected);
}

ZEST_CASE(connect_twice_fails) {
    auto receiver = bind_loopback(loop);
    auto sender = udp::create(loop);
    ASSERT(receiver.has_value());
    ASSERT(sender.has_value());
    ASSERT(!sender->connect("127.0.0.1", receiver->port));

    EXPECT(sender->connect("127.0.0.1", receiver->port) == error::socket_is_already_connected);
}

ZEST_CASE(disconnect_without_a_peer_fails) {
    auto socket = udp::create(loop);
    ASSERT(socket.has_value());

    EXPECT(socket->disconnect() == error::socket_is_not_connected);
}

ZEST_CASE(try_send_sends_at_once) {
    auto receiver = bind_loopback(loop);
    auto sender = udp::create(loop);
    ASSERT(receiver.has_value());
    ASSERT(sender.has_value());

    EXPECT(!sender->try_send(std::string_view("unconnected"), "127.0.0.1", receiver->port));
    ASSERT(!sender->connect("127.0.0.1", receiver->port));
    EXPECT(!sender->try_send(std::string_view("connected")));
    auto receive_two = [&]() -> task<std::vector<std::string>, error> {
        std::vector<std::string> data;
        for(int i = 0; i < 2; ++i) {
            data.push_back((co_await receiver->socket.recv().or_fail()).data);
        }
        // UDP keeps no order, and loopback does reorder around connect().
        std::ranges::sort(data);
        co_return data;
    };

    auto [received] = run(receive_two());
    ASSERT(received.has_value());
    EXPECT(*received == std::vector<std::string>{"connected", "unconnected"});
}

// An unconnected socket needs an address, a connected one refuses another,
// and the address must parse.
ZEST_CASE(send_to_the_wrong_destination_fails) {
    auto unconnected = udp::create(loop);
    auto connected = udp::create(loop);
    ASSERT(unconnected.has_value());
    ASSERT(connected.has_value());
    ASSERT(!connected->connect("127.0.0.1", 9));
    std::string_view data = "x";

    auto [no_address, second_address, unparsable] =
        run(unconnected->send(data),
            connected->send(data, "127.0.0.1", 9),
            unconnected->send(data, "not-an-address", 9));
    ASSERT(no_address.has_error());
    EXPECT(no_address.error() == error::destination_address_required);
    ASSERT(second_address.has_error());
    EXPECT(second_address.error() == error::socket_is_already_connected);
    ASSERT(unparsable.has_error());
    EXPECT(unparsable.error() == error::invalid_argument);
    EXPECT(unconnected->try_send(data) == error::destination_address_required);
    EXPECT(unconnected->try_send(data, "not-an-address", 9) == error::invalid_argument);
}

// libuv cannot take a send back: a cancelled send still goes out, and its
// task ends cancelled once it has. The sender binds on its first send.
ZEST_CASE(cancelled_send_still_delivers) {
    auto receiver = bind_loopback(loop);
    auto sender = udp::create(loop);
    ASSERT(receiver.has_value());
    ASSERT(sender.has_value());
    auto cancel_at_once = [&]() -> task<std::size_t, error> {
        auto sending = sender->send(std::string_view("kept"), "127.0.0.1", receiver->port);
        auto first = co_await or_fail(co_await when_any(std::move(sending), finished()));
        co_return first.index();
    };

    auto [raced, received] = run(cancel_at_once(), receiver->socket.recv());
    ASSERT(raced.has_value());
    EXPECT(*raced == 1U);
    ASSERT(received.has_value());
    EXPECT(received->data == "kept");
}

ZEST_CASE(overlapping_sends_both_go_out) {
    auto receiver = bind_loopback(loop);
    auto sender = bind_loopback(loop);
    ASSERT(receiver.has_value());
    ASSERT(sender.has_value());
    auto receive_two = [&]() -> task<std::vector<std::string>, error> {
        std::vector<std::string> data;
        for(int i = 0; i < 2; ++i) {
            data.push_back((co_await receiver->socket.recv().or_fail()).data);
        }
        std::ranges::sort(data);
        co_return data;
    };

    auto [first, second, received] =
        run(sender->socket.send(std::string_view("one"), "127.0.0.1", receiver->port),
            sender->socket.send(std::string_view("two"), "127.0.0.1", receiver->port),
            receive_two());
    EXPECT(first.has_value());
    EXPECT(second.has_value());
    ASSERT(received.has_value());
    EXPECT(*received == std::vector<std::string>{"one", "two"});
}

// Once libuv has drained the socket it calls back with zero bytes from no
// address; that is not a datagram and must not reach the next recv().
ZEST_CASE(recv_after_a_drained_socket_waits_for_the_next_datagram) {
    auto receiver = bind_loopback(loop);
    auto sender = bind_loopback(loop);
    ASSERT(receiver.has_value());
    ASSERT(sender.has_value());
    auto exchange = [&]() -> task<std::pair<std::string, std::string>, error> {
        co_await sender->socket.send(std::string_view("first"), "127.0.0.1", receiver->port)
            .or_fail();
        auto first = co_await receiver->socket.recv().or_fail();
        // Lets libuv finish the read that returned `first`, whose last call
        // reports the drained socket, before anything else can arrive.
        co_await yield();
        co_await sender->socket.send(std::string_view("second"), "127.0.0.1", receiver->port)
            .or_fail();
        auto second = co_await receiver->socket.recv().or_fail();
        co_return std::pair{std::move(first.data), std::move(second.data)};
    };

    auto [received] = run(exchange());
    ASSERT(received.has_value());
    EXPECT(received->first == "first");
    EXPECT(received->second == "second");
}

// A datagram that arrives while no recv() waits is kept for the next one.
ZEST_CASE(datagram_arriving_between_recvs_is_kept) {
    auto receiver = bind_loopback(loop);
    auto sender = bind_loopback(loop);
    ASSERT(receiver.has_value());
    ASSERT(sender.has_value());
    auto exchange = [&]() -> task<std::vector<std::string>, error> {
        co_await sender->socket.send(std::string_view("one"), "127.0.0.1", receiver->port)
            .or_fail();
        co_await sender->socket.send(std::string_view("two"), "127.0.0.1", receiver->port)
            .or_fail();
        std::vector<std::string> data{(co_await receiver->socket.recv().or_fail()).data};
        // Reading on, libuv delivers the second datagram while nothing waits.
        co_await yield();
        data.push_back((co_await receiver->socket.recv().or_fail()).data);
        std::ranges::sort(data);
        co_return data;
    };

    auto [received] = run(exchange());
    ASSERT(received.has_value());
    EXPECT(*received == std::vector<std::string>{"one", "two"});
}

ZEST_CASE(empty_datagram_arrives_empty) {
    auto receiver = bind_loopback(loop);
    auto sender = bind_loopback(loop);
    ASSERT(receiver.has_value());
    ASSERT(sender.has_value());

    auto [sent, received] =
        run(sender->socket.send(std::string_view(), "127.0.0.1", receiver->port),
            receiver->socket.recv());
    EXPECT(sent.has_value());
    ASSERT(received.has_value());
    EXPECT(received->data.empty());
    EXPECT(received->sender.port == sender->port);
}

// With the 64 KiB buffer udp keeps, a recvmmsg socket reads one datagram per
// call, marks it as a chunk, and releases the buffer with another empty
// callback.
ZEST_CASE(recvmmsg_socket_receives_datagrams) {
    auto receiver = bind_loopback(loop, {.recvmmsg = true});
    auto sender = bind_loopback(loop);
    ASSERT(receiver.has_value());
    ASSERT(sender.has_value());
    using Datagram = std::pair<std::string, bool>;
    auto exchange = [&]() -> task<std::vector<Datagram>, error> {
        co_await sender->socket.send(std::string_view("one"), "127.0.0.1", receiver->port)
            .or_fail();
        co_await sender->socket.send(std::string_view("two"), "127.0.0.1", receiver->port)
            .or_fail();
        std::vector<Datagram> datagrams;
        for(int i = 0; i < 2; ++i) {
            auto received = co_await receiver->socket.recv().or_fail();
            datagrams.emplace_back(std::move(received.data), received.flags.mmsg_chunk);
        }
        std::ranges::sort(datagrams);
        co_return datagrams;
    };

    auto [received] = run(exchange());
    ASSERT(received.has_value());
    const bool batched = receiver->socket.using_recvmmsg();
    EXPECT(*received == std::vector<Datagram>{
                            {"one", batched},
                            {"two", batched}
    });
}

ZEST_CASE(reuse_addr_lets_two_sockets_share_a_port) {
    const udp::bind_options shared{.reuse_addr = true};
    auto first = udp::create(loop);
    auto second = udp::create(loop);
    ASSERT(first.has_value());
    ASSERT(second.has_value());
    ASSERT(!first->bind("127.0.0.1", 0, shared));
    auto name = first->getsockname();
    ASSERT(name.has_value());

    EXPECT(!second->bind("127.0.0.1", name->port, shared));
}

// libuv supports reuse_port where SO_REUSEPORT balances the load, and
// refuses it on macOS and Windows.
ZEST_CASE(reuse_port_lets_two_sockets_share_a_port) {
    const udp::bind_options reuse_port{.reuse_port = true};
    auto first = udp::create(loop);
    ASSERT(first.has_value());
    auto bound = first->bind("127.0.0.1", 0, reuse_port);
#if defined(__APPLE__) || defined(_WIN32)
    EXPECT(bound == error::operation_not_supported_on_socket);
#else
    ASSERT(!bound);
    auto name = first->getsockname();
    ASSERT(name.has_value());
    auto second = udp::create(loop);
    ASSERT(second.has_value());
    EXPECT(!second->bind("127.0.0.1", name->port, reuse_port));
#endif
}

// libuv only makes an IPv6 socket IPv6-only; the flag on an IPv4 address is
// refused rather than ignored.
ZEST_CASE(ipv6_only_bind_to_an_ipv4_address_fails) {
    auto created = udp::create(loop);
    ASSERT(created.has_value());
    EXPECT(created->bind("127.0.0.1", 0, {.ipv6_only = true}) == error::invalid_argument);
}

ZEST_CASE(second_recv_while_one_is_pending_fails) {
    auto receiver = bind_loopback(loop);
    auto sender = bind_loopback(loop);
    ASSERT(receiver.has_value());
    ASSERT(sender.has_value());
    auto second_then_send = [&]() -> task<error> {
        auto second = co_await receiver->socket.recv();
        auto sent =
            co_await sender->socket.send(std::string_view("first"), "127.0.0.1", receiver->port);
        EXPECT(sent.has_value());
        co_return second.has_error() ? second.error() : error();
    };

    auto [first, second] = run(receiver->socket.recv(), second_then_send());
    ASSERT(first.has_value());
    EXPECT(first->data == "first");
    ASSERT(second.has_value());
    EXPECT(*second == error::resource_busy_or_locked);
}

// Nothing is sent before the yield, so only the cancel can end the first
// recv.
ZEST_CASE(cancelled_recv_leaves_the_socket_usable) {
    auto receiver = bind_loopback(loop);
    auto sender = bind_loopback(loop);
    ASSERT(receiver.has_value());
    ASSERT(sender.has_value());
    auto cancel_then_exchange = [&]() -> task<std::pair<std::size_t, std::string>, error> {
        auto first = co_await or_fail(co_await when_any(receiver->socket.recv(), yield()));
        co_await sender->socket.send(std::string_view("after"), "127.0.0.1", receiver->port)
            .or_fail();
        auto received = co_await receiver->socket.recv().or_fail();
        co_return std::pair{first.index(), std::move(received.data)};
    };

    auto [result] = run(cancel_then_exchange());
    ASSERT(result.has_value());
    EXPECT(result->first == 1U);
    EXPECT(result->second == "after");
}

// stop() is not sticky: the recv after it receives again.
ZEST_CASE(stop_ends_a_pending_recv) {
    auto receiver = bind_loopback(loop);
    auto sender = bind_loopback(loop);
    ASSERT(receiver.has_value());
    ASSERT(sender.has_value());
    auto stop_it = [&]() -> task<error> {
        co_return receiver->socket.stop();
    };
    auto exchange = [&]() -> task<std::string, error> {
        co_await sender->socket.send(std::string_view("after"), "127.0.0.1", receiver->port)
            .or_fail();
        co_return (co_await receiver->socket.recv().or_fail()).data;
    };

    auto [pending, stopped] = run(receiver->socket.recv(), stop_it());
    ASSERT(pending.has_error());
    EXPECT(pending.error() == error::operation_aborted);
    ASSERT(stopped.has_value());
    EXPECT(!*stopped);
    auto [received] = run(exchange());
    ASSERT(received.has_value());
    EXPECT(*received == "after");
}

ZEST_CASE(destroying_a_socket_ends_its_recv) {
    auto bound = bind_loopback(loop);
    ASSERT(bound.has_value());
    std::optional<udp> receiver = std::move(bound->socket);
    auto destroy = [&]() -> task<> {
        receiver.reset();
        co_return;
    };

    auto [received, destroyed] = run(receiver->recv(), destroy());
    ASSERT(received.has_error());
    EXPECT(received.error() == error::operation_aborted);
}

// After the first recv the socket receives on its own; of the datagrams that
// arrive while no recv waits it keeps 64 and drops the rest. A datagram
// socket pair hands what one end sends to the other before send() returns,
// and each yield gives libuv a loop turn to read it all.
#ifndef _WIN32
ZEST_CASE(datagrams_nobody_waits_for_are_kept_up_to_64) {
    int fds[2] = {-1, -1};
    ASSERT(::socketpair(AF_UNIX, SOCK_DGRAM, 0, fds) == 0);
    auto receiver = udp::open(fds[0], loop);
    ASSERT(receiver.has_value());
    int sent = 0;
    auto flood = [&]() -> task<int, error> {
        sent += ::send(fds[1], "start", 5, 0) == 5 ? 1 : 0;
        co_await receiver->recv().or_fail();
        for(int round = 0; round < 10; ++round) {
            for(int i = 0; i < 8; ++i) {
                sent += ::send(fds[1], "x", 1, 0) == 1 ? 1 : 0;
            }
            co_await yield();
        }
        int kept = 0;
        while(true) {
            auto next = co_await or_fail(co_await when_any(receiver->recv(), yield()));
            if(next.index() != 0) {
                break;
            }
            kept += 1;
        }
        co_return kept;
    };

    auto [kept] = run(flood());
    ::close(fds[1]);
    EXPECT(sent == 81);
    ASSERT(kept.has_value());
    EXPECT(*kept == 64);
}
#endif

// Linux reports the ICMP port unreachable that answers a connected socket
// to its next read. libuv on Windows ignores that error for udp but stops
// reading on others, which recv() must start again.
#ifdef __linux__
ZEST_CASE(recv_after_a_receive_error_reads_again) {
    int port = 0;
    {
        auto closed = bind_loopback(loop);
        ASSERT(closed.has_value());
        port = closed->port;
    }
    auto client = bind_loopback(loop);
    ASSERT(client.has_value());
    ASSERT(!client->socket.connect("127.0.0.1", port));
    auto refused = [&]() -> task<result<udp::recv_result>, error> {
        co_await client->socket.send(std::string_view("anyone?")).or_fail();
        co_return co_await client->socket.recv();
    };

    auto [first] = run(refused());
    ASSERT(first.has_value());
    ASSERT(first->has_error());
    EXPECT(first->error() == error::connection_refused);

    auto peer = udp::create(loop);
    ASSERT(peer.has_value());
    ASSERT(!peer->bind("127.0.0.1", port));
    auto [sent, second] =
        run(peer->send(std::string_view("here"), "127.0.0.1", client->port), client->socket.recv());
    EXPECT(sent.has_value());
    ASSERT(second.has_value());
    EXPECT(second->data == "here");
}
#endif

ZEST_CASE(bind_to_a_port_in_use_fails) {
    auto taken = bind_loopback(loop);
    auto other = udp::create(loop);
    ASSERT(taken.has_value());
    ASSERT(other.has_value());

    EXPECT(other->bind("127.0.0.1", taken->port) == error::address_already_in_use);
}

ZEST_CASE(bind_to_an_unparsable_host_fails) {
    auto socket = udp::create(loop);
    ASSERT(socket.has_value());

    EXPECT(socket->bind("not-an-address", 0) == error::invalid_argument);
}

ZEST_CASE(socket_options_can_be_set) {
    auto bound = bind_loopback(loop);
    ASSERT(bound.has_value());
    auto& socket = bound->socket;

    EXPECT(!socket.set_broadcast(true));
    EXPECT(!socket.set_ttl(32));
    EXPECT(!socket.set_multicast_loop(false));
    EXPECT(!socket.set_multicast_ttl(4));
    EXPECT(socket.send_queue_count() == 0U);
    EXPECT(socket.send_queue_size() == 0U);
}

ZEST_CASE(socket_options_out_of_range_fails) {
    auto bound = bind_loopback(loop);
    ASSERT(bound.has_value());
    auto& socket = bound->socket;

    EXPECT(socket.set_ttl(0) == error::invalid_argument);
    EXPECT(socket.set_membership("not-an-address", "", udp::membership::join) ==
           error::invalid_argument);
    EXPECT(socket.set_source_membership("not-an-address", "", "127.0.0.1", udp::membership::join) ==
           error::invalid_argument);
    EXPECT(socket.set_multicast_interface("not-an-address") == error::invalid_argument);
}

#ifndef _WIN32
ZEST_CASE(open_wraps_a_bound_socket) {
    int raw = ::socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT(raw >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT(::bind(raw, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    auto opened = udp::open(raw, loop);
    ASSERT(opened.has_value());
    auto name = opened->getsockname();
    ASSERT(name.has_value());
    auto sender = bind_loopback(loop);
    ASSERT(sender.has_value());

    auto [sent, received] =
        run(sender->socket.send(std::string_view("opened"), "127.0.0.1", name->port),
            opened->recv());
    EXPECT(sent.has_value());
    ASSERT(received.has_value());
    EXPECT(received->data == "opened");
}
#endif

};  // ZEST_SUITE(async_io_udp)

}  // namespace

}  // namespace kota
