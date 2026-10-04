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

#include "async/harness/io.h"
#include "kota/zest/async.h"
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
    if(auto err = created.bind("127.0.0.1", 0)) {
        return outcome_error(err);
    }
    auto name = created.getsockname();
    if(!name) {
        return outcome_error(name.error());
    }
    return Bound{.socket = std::move(created), .port = name->port};
}

#ifdef __linux__
/// A loopback port that was bound and released again, so nothing listens on
/// it; 0 if none could be bound.
int closed_port(event_loop& loop) {
    auto closed = bind_loopback(loop);
    return closed ? closed->port : 0;
}
#endif

ZEST_SUITE(async_io_udp, zest::LoopFixture) {

ZEST_CASE(datagram_arrives_with_its_sender) {
    auto receiver = bind_loopback(loop);
    auto sender = bind_loopback(loop);
    ZASSERT(receiver.has_value());
    ZASSERT(sender.has_value());
    ZEXPECT(receiver->port > 0);

    auto [sent, received] =
        run(sender->socket.send(std::string_view("kotatsu-udp"), "127.0.0.1", receiver->port),
            receiver->socket.recv());
    ZEXPECT(sent.has_value());
    ZASSERT(received.has_value());
    ZEXPECT(received->data == "kotatsu-udp");
    ZEXPECT(received->sender.addr == "127.0.0.1");
    ZEXPECT(received->sender.port == sender->port);
    ZEXPECT(!received->flags.partial);
}

ZEST_CASE(connected_socket_sends_to_its_peer) {
    auto receiver = bind_loopback(loop);
    auto sender = udp::create(loop);
    ZASSERT(receiver.has_value());
    ZASSERT(!sender.connect("127.0.0.1", receiver->port));
    auto peer = sender.getpeername();
    ZASSERT(peer.has_value());
    ZEXPECT(peer->port == receiver->port);

    auto [sent, received] =
        run(sender.send(std::string_view("kotatsu-connected")), receiver->socket.recv());
    ZEXPECT(sent.has_value());
    ZASSERT(received.has_value());
    ZEXPECT(received->data == "kotatsu-connected");
}

ZEST_CASE(disconnect_forgets_the_peer) {
    auto receiver = bind_loopback(loop);
    auto sender = udp::create(loop);
    ZASSERT(receiver.has_value());
    ZASSERT(!sender.connect("127.0.0.1", receiver->port));

    ZEXPECT(!sender.disconnect());
    auto peer = sender.getpeername();
    ZASSERT(peer.has_error());
    ZEXPECT(peer.error() == error::socket_is_not_connected);
}

ZEST_CASE(connect_twice_fails) {
    auto receiver = bind_loopback(loop);
    auto sender = udp::create(loop);
    ZASSERT(receiver.has_value());
    ZASSERT(!sender.connect("127.0.0.1", receiver->port));

    ZEXPECT(sender.connect("127.0.0.1", receiver->port) == error::socket_is_already_connected);
}

ZEST_CASE(disconnect_without_a_peer_fails) {
    auto socket = udp::create(loop);

    ZEXPECT(socket.disconnect() == error::socket_is_not_connected);
}

ZEST_CASE(try_send_sends_at_once) {
    auto receiver = bind_loopback(loop);
    auto sender = udp::create(loop);
    ZASSERT(receiver.has_value());

    ZEXPECT(!sender.try_send(std::string_view("unconnected"), "127.0.0.1", receiver->port));
    ZASSERT(!sender.connect("127.0.0.1", receiver->port));
    ZEXPECT(!sender.try_send(std::string_view("connected")));
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
    ZASSERT(received.has_value());
    ZEXPECT(*received == std::vector<std::string>{"connected", "unconnected"});
}

// An unconnected socket needs an address, a connected one refuses another,
// and the address must parse.
ZEST_CASE(send_to_the_wrong_destination_fails) {
    auto unconnected = udp::create(loop);
    auto connected = udp::create(loop);
    ZASSERT(!connected.connect("127.0.0.1", 9));
    std::string_view data = "x";

    auto [no_address, second_address, unparsable] =
        run(unconnected.send(data),
            connected.send(data, "127.0.0.1", 9),
            unconnected.send(data, "not-an-address", 9));
    ZASSERT(no_address.has_error());
    ZEXPECT(no_address.error() == error::destination_address_required);
    ZASSERT(second_address.has_error());
    ZEXPECT(second_address.error() == error::socket_is_already_connected);
    ZASSERT(unparsable.has_error());
    ZEXPECT(unparsable.error() == error::invalid_argument);
    ZEXPECT(unconnected.try_send(data) == error::destination_address_required);
    ZEXPECT(unconnected.try_send(data, "not-an-address", 9) == error::invalid_argument);
}

// libuv cannot take a send back: a cancelled send still goes out, and its
// task ends cancelled once it has, never resuming past it. The sender binds
// on its first send.
ZEST_CASE(cancelled_send_still_delivers) {
    auto receiver = bind_loopback(loop);
    auto sender = udp::create(loop);
    ZASSERT(receiver.has_value());
    bool resumed = false;
    auto send = [&]() -> task<> {
        [[maybe_unused]] auto sent =
            co_await sender.send(std::string_view("kept"), "127.0.0.1", receiver->port);
        resumed = true;
    };

    auto [raced, received] = run(test::winner(send(), test::finished()), receiver->socket.recv());
    ZASSERT(raced.has_value());
    ZEXPECT(*raced == 1U);
    ZEXPECT(!resumed);
    ZASSERT(received.has_value());
    ZEXPECT(received->data == "kept");
}

ZEST_CASE(overlapping_sends_both_go_out) {
    auto receiver = bind_loopback(loop);
    auto sender = bind_loopback(loop);
    ZASSERT(receiver.has_value());
    ZASSERT(sender.has_value());
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
    ZEXPECT(first.has_value());
    ZEXPECT(second.has_value());
    ZASSERT(received.has_value());
    ZEXPECT(*received == std::vector<std::string>{"one", "two"});
}

// Once libuv has drained the socket it calls back with zero bytes from no
// address; that is not a datagram and must not reach the next recv().
ZEST_CASE(recv_after_a_drained_socket_waits_for_the_next_datagram) {
    auto receiver = bind_loopback(loop);
    auto sender = bind_loopback(loop);
    ZASSERT(receiver.has_value());
    ZASSERT(sender.has_value());
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
    ZASSERT(received.has_value());
    ZEXPECT(received->first == "first");
    ZEXPECT(received->second == "second");
}

// A datagram that arrives while no recv() waits is kept for the next one.
ZEST_CASE(datagram_arriving_between_recvs_is_kept) {
    auto receiver = bind_loopback(loop);
    auto sender = bind_loopback(loop);
    ZASSERT(receiver.has_value());
    ZASSERT(sender.has_value());
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
    ZASSERT(received.has_value());
    ZEXPECT(*received == std::vector<std::string>{"one", "two"});
}

ZEST_CASE(empty_datagram_arrives_empty) {
    auto receiver = bind_loopback(loop);
    auto sender = bind_loopback(loop);
    ZASSERT(receiver.has_value());
    ZASSERT(sender.has_value());

    auto [sent, received] =
        run(sender->socket.send(std::string_view(), "127.0.0.1", receiver->port),
            receiver->socket.recv());
    ZEXPECT(sent.has_value());
    ZASSERT(received.has_value());
    ZEXPECT(received->data.empty());
    ZEXPECT(received->sender.port == sender->port);
}

// With the 64 KiB buffer udp keeps, a recvmmsg socket reads one datagram per
// call, marks it as a chunk, and releases the buffer with another empty
// callback.
ZEST_CASE(recvmmsg_socket_receives_datagrams) {
    auto receiver = bind_loopback(loop, {.recvmmsg = true});
    auto sender = bind_loopback(loop);
    ZASSERT(receiver.has_value());
    ZASSERT(sender.has_value());
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
    ZASSERT(received.has_value());
    const bool batched = receiver->socket.using_recvmmsg();
    ZEXPECT(*received == std::vector<Datagram>{
                             {"one", batched},
                             {"two", batched}
    });
}

ZEST_CASE(reuse_addr_lets_two_sockets_share_a_port) {
    const udp::bind_options shared{.reuse_addr = true};
    auto first = udp::create(loop);
    auto second = udp::create(loop);
    ZASSERT(!first.bind("127.0.0.1", 0, shared));
    auto name = first.getsockname();
    ZASSERT(name.has_value());

    ZEXPECT(!second.bind("127.0.0.1", name->port, shared));
}

// libuv supports reuse_port where SO_REUSEPORT balances the load, and
// refuses it on macOS and Windows.
ZEST_CASE(reuse_port_lets_two_sockets_share_a_port) {
    const udp::bind_options reuse_port{.reuse_port = true};
    auto first = udp::create(loop);
    auto bound = first.bind("127.0.0.1", 0, reuse_port);
#if defined(__APPLE__) || defined(_WIN32)
    ZEXPECT(bound == error::operation_not_supported_on_socket);
#else
    ZASSERT(!bound);
    auto name = first.getsockname();
    ZASSERT(name.has_value());
    auto second = udp::create(loop);
    ZEXPECT(!second.bind("127.0.0.1", name->port, reuse_port));
#endif
}

// libuv only makes an IPv6 socket IPv6-only; the flag on an IPv4 address is
// refused rather than ignored.
ZEST_CASE(ipv6_only_bind_to_an_ipv4_address_fails) {
    auto created = udp::create(loop);
    ZEXPECT(created.bind("127.0.0.1", 0, {.ipv6_only = true}) == error::invalid_argument);
}

ZEST_CASE(second_recv_while_one_is_pending_fails) {
    auto receiver = bind_loopback(loop);
    auto sender = bind_loopback(loop);
    ZASSERT(receiver.has_value());
    ZASSERT(sender.has_value());
    auto second_then_send = [&]() -> task<error> {
        auto second = co_await receiver->socket.recv();
        auto sent =
            co_await sender->socket.send(std::string_view("first"), "127.0.0.1", receiver->port);
        ZEXPECT(sent.has_value());
        co_return second.has_error() ? second.error() : error();
    };

    auto [first, second] = run(receiver->socket.recv(), second_then_send());
    ZASSERT(first.has_value());
    ZEXPECT(first->data == "first");
    ZASSERT(second.has_value());
    ZEXPECT(*second == error::resource_busy_or_locked);
}

// Nothing is sent before the yield, so only the cancel can end the first
// recv.
ZEST_CASE(cancelled_recv_leaves_the_socket_usable) {
    auto receiver = bind_loopback(loop);
    auto sender = bind_loopback(loop);
    ZASSERT(receiver.has_value());
    ZASSERT(sender.has_value());
    auto cancel_then_exchange = [&]() -> task<std::pair<std::size_t, std::string>, error> {
        auto first = co_await or_fail(co_await when_any(receiver->socket.recv(), yield()));
        co_await sender->socket.send(std::string_view("after"), "127.0.0.1", receiver->port)
            .or_fail();
        auto received = co_await receiver->socket.recv().or_fail();
        co_return std::pair{first.index(), std::move(received.data)};
    };

    auto [result] = run(cancel_then_exchange());
    ZASSERT(result.has_value());
    ZEXPECT(result->first == 1U);
    ZEXPECT(result->second == "after");
}

// stop() is not sticky: the recv after it receives again.
ZEST_CASE(stop_ends_a_pending_recv) {
    auto receiver = bind_loopback(loop);
    auto sender = bind_loopback(loop);
    ZASSERT(receiver.has_value());
    ZASSERT(sender.has_value());
    auto stop_it = [&]() -> task<error> {
        co_return receiver->socket.stop();
    };
    auto exchange = [&]() -> task<std::string, error> {
        co_await sender->socket.send(std::string_view("after"), "127.0.0.1", receiver->port)
            .or_fail();
        co_return (co_await receiver->socket.recv().or_fail()).data;
    };

    auto [pending, stopped] = run(receiver->socket.recv(), stop_it());
    ZASSERT(pending.has_error());
    ZEXPECT(pending.error() == error::operation_aborted);
    ZASSERT(stopped.has_value());
    ZEXPECT(!*stopped);
    auto [received] = run(exchange());
    ZASSERT(received.has_value());
    ZEXPECT(*received == "after");
}

ZEST_CASE(recv_ended_by_destroying_its_socket_fails) {
    auto bound = bind_loopback(loop);
    ZASSERT(bound.has_value());
    std::optional<udp> receiver = std::move(bound->socket);
    auto destroy = [&]() -> task<> {
        receiver.reset();
        co_return;
    };

    auto [received, destroyed] = run(receiver->recv(), destroy());
    ZASSERT(received.has_error());
    ZEXPECT(received.error() == error::operation_aborted);
}

// The destroyed socket's recv is cancelled after its destruction has ended
// it, before the loop has resumed it: the cancel leaves that ending alone.
ZEST_CASE(recv_cancelled_after_its_socket_is_destroyed_ends) {
    auto bound = bind_loopback(loop);
    ZASSERT(bound.has_value());
    std::optional<udp> receiver = std::move(bound->socket);
    auto destroy = [&]() -> task<> {
        receiver.reset();
        co_return;
    };

    auto [result] = run(test::winner(receiver->recv(), destroy()));
    ZASSERT(result.has_value());
    ZEXPECT(*result == 1U);
}

// Socket pairs are POSIX only. A datagram socket pair hands what one end
// sends to the other before send() returns.
#ifndef _WIN32
// A datagram wakes the pending recv, which resumes a few loop turns later; a
// recv made before then fails, and the datagram stays the first one's. The
// yield resumes after the loop has read the datagram, and before the first
// recv, which the datagram queued after it.
ZEST_CASE(recv_while_a_woken_one_has_not_resumed_fails) {
    int fds[2] = {-1, -1};
    ZASSERT(::socketpair(AF_UNIX, SOCK_DGRAM, 0, fds) == 0);
    auto receiver = udp::open(fds[0], loop);
    ZASSERT(receiver.has_value());
    auto second = [&]() -> task<error> {
        ZEXPECT(::send(fds[1], "first", 5, MSG_DONTWAIT) == 5);
        co_await yield();
        auto received = co_await receiver->recv();
        co_return received.has_error() ? received.error() : error();
    };

    auto [first, busy] = run(receiver->recv(), second());
    ::close(fds[1]);
    ZASSERT(first.has_value());
    ZEXPECT(first->data == "first");
    ZASSERT(busy.has_value());
    ZEXPECT(*busy == error::resource_busy_or_locked);
}

// After the first recv the socket receives on its own; of the datagrams that
// arrive while no recv waits it keeps 64 and drops the rest. Each yield
// gives libuv a loop turn to read what was sent. The receiving end holds up
// to net.unix.max_dgram_qlen datagrams (10 by default), so each round sends
// fewer than that.
ZEST_CASE(datagrams_nobody_waits_for_are_kept_up_to_64) {
    int fds[2] = {-1, -1};
    ZASSERT(::socketpair(AF_UNIX, SOCK_DGRAM, 0, fds) == 0);
    auto receiver = udp::open(fds[0], loop);
    ZASSERT(receiver.has_value());
    int sent = 0;
    auto flood = [&]() -> task<int, error> {
        sent += ::send(fds[1], "start", 5, MSG_DONTWAIT) == 5 ? 1 : 0;
        co_await receiver->recv().or_fail();
        for(int round = 0; round < 10; ++round) {
            for(int i = 0; i < 8; ++i) {
                sent += ::send(fds[1], "x", 1, MSG_DONTWAIT) == 1 ? 1 : 0;
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
    ZEXPECT(sent == 81);
    ZASSERT(kept.has_value());
    ZEXPECT(*kept == 64);
}
#endif

#ifdef __linux__
// Linux reports the ICMP port unreachable that answers a connected socket
// to its next read. libuv on Windows ignores that error for udp but stops
// reading on others, which recv() must start again.
ZEST_CASE(recv_after_a_receive_error_reads_again) {
    int port = closed_port(loop);
    ZASSERT(port > 0);
    auto client = bind_loopback(loop);
    ZASSERT(client.has_value());
    ZASSERT(!client->socket.connect("127.0.0.1", port));
    auto refused = [&]() -> task<result<udp::recv_result>, error> {
        co_await client->socket.send(std::string_view("anyone?")).or_fail();
        co_return co_await client->socket.recv();
    };

    auto [first] = run(refused());
    ZASSERT(first.has_value());
    ZASSERT(first->has_error());
    ZEXPECT(first->error() == error::connection_refused);

    auto peer = udp::create(loop);
    ZASSERT(!peer.bind("127.0.0.1", port));
    auto [sent, second] =
        run(peer.send(std::string_view("here"), "127.0.0.1", client->port), client->socket.recv());
    ZEXPECT(sent.has_value());
    ZASSERT(second.has_value());
    ZEXPECT(second->data == "here");
}

// The ICMP error comes with an EPOLLERR, after which libuv reads the
// socket's error queue and asserts that the socket still receives: the task
// the error wakes must be free to stop it all the same.
ZEST_CASE(task_woken_by_a_receive_error_can_stop_the_socket) {
    int port = closed_port(loop);
    ZASSERT(port > 0);
    auto client = bind_loopback(loop);
    ZASSERT(client.has_value());
    ZASSERT(!client->socket.connect("127.0.0.1", port));
    auto refused_then_stop = [&]() -> task<std::pair<error, error>, error> {
        co_await client->socket.send(std::string_view("anyone?")).or_fail();
        auto received = co_await client->socket.recv();
        auto refused = received.has_error() ? received.error() : error();
        co_return std::pair{refused, client->socket.stop()};
    };

    auto [result] = run(refused_then_stop());
    ZASSERT(result.has_value());
    ZEXPECT(result->first == error::connection_refused);
    ZEXPECT(!result->second);
}

// Once try_send() finds the peer's end of a datagram socket pair full, a
// send waits in libuv's queue, and closing the socket ends it: the send
// fails with operation_aborted rather than cancelling its task. Linux only:
// how a full datagram socket pair answers a send differs between systems.
ZEST_CASE(send_ended_by_closing_its_socket_fails) {
    int fds[2] = {-1, -1};
    ZASSERT(::socketpair(AF_UNIX, SOCK_DGRAM, 0, fds) == 0);
    auto opened = udp::open(fds[0], loop);
    ZASSERT(opened.has_value());
    std::optional<udp> sender = std::move(*opened);
    error full;
    for(int i = 0; i < 100'000 && !full; ++i) {
        full = sender->try_send(std::string_view("x"));
    }
    ZASSERT(full == error::resource_temporarily_unavailable);
    auto close = [&]() -> task<> {
        sender.reset();
        co_return;
    };

    auto [sent, closed] = run(sender->send(std::string_view("queued")), close());
    ::close(fds[1]);
    ZASSERT(sent.has_error());
    ZEXPECT(sent.error() == error::operation_aborted);
}
#endif

ZEST_CASE(inert_socket_fails) {
    udp inert;
    std::string_view data = "x";

    auto [received, sent_to, sent] =
        run(inert.recv(), inert.send(data, "127.0.0.1", 9), inert.send(data));
    ZASSERT(received.has_error());
    ZEXPECT(received.error() == error::invalid_argument);
    ZASSERT(sent_to.has_error());
    ZEXPECT(sent_to.error() == error::invalid_argument);
    ZASSERT(sent.has_error());
    ZEXPECT(sent.error() == error::invalid_argument);
    ZEXPECT(inert.bind("127.0.0.1", 0) == error::invalid_argument);
    ZEXPECT(inert.connect("127.0.0.1", 9) == error::invalid_argument);
    ZEXPECT(inert.disconnect() == error::invalid_argument);
    ZEXPECT(inert.try_send(data, "127.0.0.1", 9) == error::invalid_argument);
    ZEXPECT(inert.try_send(data) == error::invalid_argument);
    ZEXPECT(inert.stop() == error::invalid_argument);
    ZEXPECT(inert.set_ttl(32) == error::invalid_argument);
    auto name = inert.getsockname();
    ZASSERT(name.has_error());
    ZEXPECT(name.error() == error::invalid_argument);
    auto peer = inert.getpeername();
    ZASSERT(peer.has_error());
    ZEXPECT(peer.error() == error::invalid_argument);
}

ZEST_CASE(bind_to_a_port_in_use_fails) {
    auto taken = bind_loopback(loop);
    auto other = udp::create(loop);
    ZASSERT(taken.has_value());

    ZEXPECT(other.bind("127.0.0.1", taken->port) == error::address_already_in_use);
}

ZEST_CASE(bind_to_an_unparsable_host_fails) {
    auto socket = udp::create(loop);

    ZEXPECT(socket.bind("not-an-address", 0) == error::invalid_argument);
}

ZEST_CASE(socket_options_can_be_set) {
    auto bound = bind_loopback(loop);
    ZASSERT(bound.has_value());
    auto& socket = bound->socket;

    ZEXPECT(!socket.set_broadcast(true));
    ZEXPECT(!socket.set_ttl(32));
    ZEXPECT(!socket.set_multicast_loop(false));
    ZEXPECT(!socket.set_multicast_ttl(4));
    ZEXPECT(socket.send_queue_count() == 0U);
    ZEXPECT(socket.send_queue_size() == 0U);
}

ZEST_CASE(socket_options_out_of_range_fails) {
    auto bound = bind_loopback(loop);
    ZASSERT(bound.has_value());
    auto& socket = bound->socket;

    ZEXPECT(socket.set_ttl(0) == error::invalid_argument);
    ZEXPECT(socket.set_membership("not-an-address", "", udp::membership::join) ==
            error::invalid_argument);
    ZEXPECT(
        socket.set_source_membership("not-an-address", "", "127.0.0.1", udp::membership::join) ==
        error::invalid_argument);
    ZEXPECT(socket.set_multicast_interface("not-an-address") == error::invalid_argument);
}

#ifndef _WIN32
ZEST_CASE(open_wraps_a_bound_socket) {
    int raw = ::socket(AF_INET, SOCK_DGRAM, 0);
    ZASSERT(raw >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ZASSERT(::bind(raw, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    auto opened = udp::open(raw, loop);
    ZASSERT(opened.has_value());
    auto name = opened->getsockname();
    ZASSERT(name.has_value());
    auto sender = bind_loopback(loop);
    ZASSERT(sender.has_value());

    auto [sent, received] =
        run(sender->socket.send(std::string_view("opened"), "127.0.0.1", name->port),
            opened->recv());
    ZEXPECT(sent.has_value());
    ZASSERT(received.has_value());
    ZEXPECT(received->data == "opened");
}
#endif

};  // ZEST_SUITE(async_io_udp)

}  // namespace

}  // namespace kota
