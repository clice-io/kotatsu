#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
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
    return Bound{std::move(*created), name->port};
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
    EXPECT(received->addr == "127.0.0.1");
    EXPECT(received->port == sender->port);
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
    auto sending = sender->send(std::string_view("kept"), "127.0.0.1", receiver->port);
    auto* node = sending.operator->();
    auto cancel_it = [&]() -> task<> {
        node->cancel();
        co_return;
    };

    auto [sent, driver, received] = run(std::move(sending), cancel_it(), receiver->socket.recv());
    EXPECT(sent.is_cancelled());
    ASSERT(received.has_value());
    EXPECT(received->data == "kept");
}

ZEST_CASE(second_send_while_one_is_in_flight_fails) {
    auto receiver = bind_loopback(loop);
    auto sender = bind_loopback(loop);
    ASSERT(receiver.has_value());
    ASSERT(sender.has_value());

    auto [first, second] =
        run(sender->socket.send(std::string_view("one"), "127.0.0.1", receiver->port),
            sender->socket.send(std::string_view("two"), "127.0.0.1", receiver->port));
    EXPECT(first.has_value());
    ASSERT(second.has_error());
    EXPECT(second.error() == error::connection_already_in_progress);
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
    EXPECT(received->port == sender->port);
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
    udp::bind_options shared(false, true);
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
    udp::bind_options reuse_port(false, false, true);
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

ZEST_CASE(second_recv_while_one_is_pending_fails) {
    auto receiver = bind_loopback(loop);
    ASSERT(receiver.has_value());
    auto first = receiver->socket.recv();
    auto* node = first.operator->();
    auto second_then_cancel = [&]() -> task<result<udp::recv_result>> {
        auto second = co_await receiver->socket.recv();
        node->cancel();
        co_return second;
    };

    auto [pending, second] = run(std::move(first), second_then_cancel());
    EXPECT(pending.is_cancelled());
    ASSERT(second.has_value());
    ASSERT(second->has_error());
    EXPECT(second->error() == error::connection_already_in_progress);
}

ZEST_CASE(cancelled_recv_leaves_the_socket_usable) {
    auto receiver = bind_loopback(loop);
    auto sender = bind_loopback(loop);
    ASSERT(receiver.has_value());
    ASSERT(sender.has_value());
    auto first = receiver->socket.recv();
    auto* node = first.operator->();
    auto cancel_then_exchange = [&]() -> task<std::string, error> {
        node->cancel();
        co_await sender->socket.send(std::string_view("after"), "127.0.0.1", receiver->port)
            .or_fail();
        auto received = co_await receiver->socket.recv().or_fail();
        co_return std::move(received.data);
    };

    auto [cancelled, received] = run(std::move(first), cancel_then_exchange());
    EXPECT(cancelled.is_cancelled());
    ASSERT(received.has_value());
    EXPECT(*received == "after");
}

// Open question, kept to document current behaviour: stop_recv() stops
// reading but leaves a pending recv() waiting, where stream::stop() and
// acceptor::stop() end theirs with operation_aborted. Only cancelling it
// ends it.
ZEST_CASE(stop_recv_leaves_a_pending_recv_waiting) {
    auto receiver = bind_loopback(loop);
    ASSERT(receiver.has_value());
    bool ended = false;
    auto waiting = [&]() -> task<result<udp::recv_result>> {
        auto received = co_await receiver->socket.recv();
        ended = true;
        co_return received;
    };
    auto pending = waiting();
    auto* node = pending.operator->();
    auto stop_then_cancel = [&]() -> task<std::pair<error, bool>> {
        auto stopped = receiver->socket.stop_recv();
        co_await yield();
        bool ended_by_stop = ended;
        node->cancel();
        co_return std::pair{stopped, ended_by_stop};
    };

    auto [cancelled, driver] = run(std::move(pending), stop_then_cancel());
    EXPECT(cancelled.is_cancelled());
    ASSERT(driver.has_value());
    EXPECT(!driver->first);
    EXPECT(!driver->second);
}

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

ZEST_CASE(socket_options_out_of_range_fail) {
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
