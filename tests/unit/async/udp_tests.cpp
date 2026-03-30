#include <string>
#include <string_view>

#include "loop_fixture.h"
#include "eventide/zest/zest.h"

namespace eventide {

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

TEST_SUITE(udp_io, loop_fixture) {

TEST_CASE(send_and_recv) {
    auto recv_sock = udp::create(loop);
    ASSERT_TRUE(recv_sock.has_value());

    auto bind_ec = recv_sock->bind("127.0.0.1", 0);
    EXPECT_FALSE(static_cast<bool>(bind_ec));

    auto endpoint = recv_sock->getsockname();
    ASSERT_TRUE(endpoint.has_value());

    auto send_sock = udp::create(loop);
    ASSERT_TRUE(send_sock.has_value());

    int done = 0;
    auto receiver = recv_once(*recv_sock, done);
    auto sender = send_to(*send_sock, "eventide-udp", endpoint->addr, endpoint->port, done);
    schedule_all(receiver, sender);

    auto recv_result = receiver.result();
    EXPECT_TRUE(recv_result.has_value());
    EXPECT_EQ(recv_result->data, "eventide-udp");

    auto send_result = sender.result();
    EXPECT_FALSE(send_result.has_error());
}

TEST_CASE(connect_and_send) {
    auto recv_sock = udp::create(loop);
    ASSERT_TRUE(recv_sock.has_value());

    auto bind_ec = recv_sock->bind("127.0.0.1", 0);
    EXPECT_FALSE(static_cast<bool>(bind_ec));

    auto endpoint = recv_sock->getsockname();
    ASSERT_TRUE(endpoint.has_value());

    auto send_sock = udp::create(loop);
    ASSERT_TRUE(send_sock.has_value());

    auto conn_ec = send_sock->connect(endpoint->addr, endpoint->port);
    EXPECT_FALSE(static_cast<bool>(conn_ec));

    int done = 0;
    auto receiver = recv_once(*recv_sock, done);
    auto sender = send_connected(*send_sock, "eventide-udp-connect", done);
    schedule_all(receiver, sender);

    auto recv_result = receiver.result();
    EXPECT_TRUE(recv_result.has_value());
    EXPECT_EQ(recv_result->data, "eventide-udp-connect");

    auto send_result = sender.result();
    EXPECT_FALSE(send_result.has_error());
}

};  // TEST_SUITE(udp_io)

}  // namespace eventide
