#include <array>
#include <optional>
#include <string>
#include <utility>

#include "eventide/ipc/transport.h"
#include "eventide/zest/zest.h"
#include "eventide/async/loop.h"
#include "eventide/async/stream.h"

#include "../common/fd_helpers.h"
#include "test_transport.h"

namespace eventide::ipc {

using test::create_pipe;
using test::close_fd;
using test::write_fd;

namespace {

task<std::pair<std::optional<std::string>, std::optional<std::string>>>
    read_two_messages(StreamTransport& transport) {
    auto first = co_await transport.read_message();
    auto second = co_await transport.read_message();
    event_loop::current().stop();
    co_return std::pair{std::move(first), std::move(second)};
}

}  // namespace

TEST_SUITE(ipc_transport) {

TEST_CASE(stream_transport_reads_back_to_back_messages) {
    event_loop loop;

    int fds[2] = {-1, -1};
    ASSERT_EQ(create_pipe(fds), 0);

    auto input = pipe::open(fds[0], pipe::options{}, loop);
    ASSERT_TRUE(input.has_value());

    StreamTransport transport(stream(std::move(*input)));

    const std::string first_payload =
        R"({"jsonrpc":"2.0","method":"example/note","params":{"text":"first"}})";
    const std::string second_payload = R"({"jsonrpc":"2.0","id":1,"result":{"sum":9}})";
    const auto payload = frame(first_payload) + frame(second_payload);

    ASSERT_EQ(write_fd(fds[1], payload.data(), payload.size()),
              static_cast<ssize_t>(payload.size()));
    ASSERT_EQ(close_fd(fds[1]), 0);

    auto reader = read_two_messages(transport);
    loop.schedule(reader);
    loop.run();

    const auto [first, second] = reader.result();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, first_payload);
    EXPECT_EQ(*second, second_payload);
}

};  // TEST_SUITE(ipc_transport)

}  // namespace eventide::ipc
