#include <string>
#include <string_view>

#include "../loop_fixture.h"
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

task<std::pair<std::string, std::string>, error> recv_two(udp& sock, int& done) {
    auto first = co_await sock.recv();
    if(!first) {
        bump_and_stop(done, 2);
        co_await fail(first.error());
    }

    auto second = co_await sock.recv();
    bump_and_stop(done, 2);
    if(!second) {
        co_await fail(second.error());
    }

    co_return std::pair{first->data, second->data};
}

task<void, error> send_two(udp& sock, std::string_view host, int port, int& done) {
    std::string_view one = "kotatsu-seq-one";
    auto first = co_await sock.send(std::span<const char>(one.data(), one.size()), host, port);
    if(first.has_error()) {
        bump_and_stop(done, 2);
        co_await fail(std::move(first).error());
    }

    // Let the receiver drain the first burst before the second send: libuv
    // ends each burst with a "nothing to read" callback that must not be
    // surfaced as an (empty) datagram to the next recv().
    co_await sleep(20);

    std::string_view two = "kotatsu-seq-two";
    auto second = co_await sock.send(std::span<const char>(two.data(), two.size()), host, port);
    bump_and_stop(done, 2);
    co_await or_fail(second);
}

}  // namespace

TEST_SUITE(udp_io, loop_fixture) {

TEST_CASE(recv_sequential_datagrams) {
    auto recv_sock = udp::create(loop);
    ASSERT_TRUE(recv_sock.has_value());

    auto bind_ec = recv_sock->bind("127.0.0.1", 0);
    EXPECT_FALSE(static_cast<bool>(bind_ec));

    auto endpoint = recv_sock->getsockname();
    ASSERT_TRUE(endpoint.has_value());

    auto send_sock = udp::create(loop);
    ASSERT_TRUE(send_sock.has_value());

    int done = 0;
    auto receiver = recv_two(*recv_sock, done);
    auto sender = send_two(*send_sock, endpoint->addr, endpoint->port, done);
    schedule_all(receiver, sender);

    auto recv_result = receiver.result();
    ASSERT_TRUE(recv_result.has_value());
    EXPECT_EQ(recv_result->first, "kotatsu-seq-one");
    EXPECT_EQ(recv_result->second, "kotatsu-seq-two");

    auto send_result = sender.result();
    EXPECT_FALSE(send_result.has_error());
}

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
    auto sender = send_to(*send_sock, "kotatsu-udp", endpoint->addr, endpoint->port, done);
    schedule_all(receiver, sender);

    auto recv_result = receiver.result();
    EXPECT_TRUE(recv_result.has_value());
    EXPECT_EQ(recv_result->data, "kotatsu-udp");

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
    auto sender = send_connected(*send_sock, "kotatsu-udp-connect", done);
    schedule_all(receiver, sender);

    auto recv_result = receiver.result();
    EXPECT_TRUE(recv_result.has_value());
    EXPECT_EQ(recv_result->data, "kotatsu-udp-connect");

    auto send_result = sender.result();
    EXPECT_FALSE(send_result.has_error());
}

};  // TEST_SUITE(udp_io)

}  // namespace kota
