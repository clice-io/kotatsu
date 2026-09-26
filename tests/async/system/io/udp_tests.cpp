#include <string>
#include <string_view>

#include "async/harness/loop_fixture.h"
#include "kota/zest/zest.h"

namespace kota {

namespace {

bool bump_and_stop(int& done, int target) {
    done += 1;
    if(done == target) {
        event_loop::current().stop();
        return true;
    }
    return false;
}

task<udp::recv_result, error> recv_once(udp& sock, int& done) {
    auto res = co_await sock.recv();
    bump_and_stop(done, 2);
    co_return res;
}

task<void, error>
    send_to(udp& sock, std::string_view payload, std::string_view host, int port, int& done) {
    std::span<const char> data(payload.data(), payload.size());
    auto ec = co_await sock.send(data, host, port);
    bump_and_stop(done, 2);
    co_await or_fail(ec);
}

task<void, error> send_connected(udp& sock, std::string_view payload, int& done) {
    std::span<const char> data(payload.data(), payload.size());
    auto ec = co_await sock.send(data);
    bump_and_stop(done, 2);
    co_await or_fail(ec);
}

}  // namespace

ZEST_SUITE(async_io_udp, loop_fixture) {

ZEST_CASE(send_and_recv) {
    auto recv_sock = udp::create(loop);
    ASSERT(recv_sock);

    auto bind_ec = recv_sock->bind("127.0.0.1", 0);
    EXPECT(!static_cast<bool>(bind_ec));

    auto endpoint = recv_sock->getsockname();
    ASSERT(endpoint);

    auto send_sock = udp::create(loop);
    ASSERT(send_sock);

    int done = 0;
    auto receiver = recv_once(*recv_sock, done);
    auto sender = send_to(*send_sock, "kotatsu-udp", endpoint->addr, endpoint->port, done);
    schedule_all(receiver, sender);

    auto recv_result = receiver.result();
    EXPECT(recv_result);
    EXPECT(recv_result->data == "kotatsu-udp");

    auto send_result = sender.result();
    EXPECT(!send_result.has_error());
}

ZEST_CASE(connect_and_send) {
    auto recv_sock = udp::create(loop);
    ASSERT(recv_sock);

    auto bind_ec = recv_sock->bind("127.0.0.1", 0);
    EXPECT(!static_cast<bool>(bind_ec));

    auto endpoint = recv_sock->getsockname();
    ASSERT(endpoint);

    auto send_sock = udp::create(loop);
    ASSERT(send_sock);

    auto conn_ec = send_sock->connect(endpoint->addr, endpoint->port);
    EXPECT(!static_cast<bool>(conn_ec));

    int done = 0;
    auto receiver = recv_once(*recv_sock, done);
    auto sender = send_connected(*send_sock, "kotatsu-udp-connect", done);
    schedule_all(receiver, sender);

    auto recv_result = receiver.result();
    EXPECT(recv_result);
    EXPECT(recv_result->data == "kotatsu-udp-connect");

    auto send_result = sender.result();
    EXPECT(!send_result.has_error());
}

// Once libuv has drained the socket it calls back with zero bytes from no
// address; that is not a datagram and must not reach the next recv().
ZEST_CASE(recv_after_drained_socket_waits_for_next_datagram) {
    auto recv_sock = udp::create(loop);
    ASSERT(recv_sock);
    ASSERT(!recv_sock->bind("127.0.0.1", 0));
    auto endpoint = recv_sock->getsockname();
    ASSERT(endpoint);

    auto send_sock = udp::create(loop);
    ASSERT(send_sock);

    auto exchange = [&]() -> task<std::pair<std::string, std::string>, error> {
        std::string_view first = "first";
        std::string_view second = "second";
        co_await send_sock->send(first, endpoint->addr, endpoint->port).or_fail();
        auto got_first = co_await recv_sock->recv().or_fail();
        co_await send_sock->send(second, endpoint->addr, endpoint->port).or_fail();
        auto got_second = co_await recv_sock->recv().or_fail();
        event_loop::current().stop();
        co_return std::pair{std::move(got_first.data), std::move(got_second.data)};
    };

    auto worker = exchange();
    schedule_all(worker);

    auto received = worker.result();
    ASSERT(received);
    EXPECT(received->first == "first");
    EXPECT(received->second == "second");
}

};  // ZEST_SUITE(async_io_udp)

}  // namespace kota
