#include <array>
#include <optional>
#include <string>
#include <utility>

#include "eventide/jsonrpc/transport.h"
#include "eventide/zest/zest.h"
#include "eventide/async/loop.h"
#include "eventide/async/stream.h"

#ifdef _WIN32
#include <BaseTsd.h>
#include <fcntl.h>
#include <io.h>
using ssize_t = SSIZE_T;
#else
#include <unistd.h>
#endif

namespace eventide::jsonrpc {

namespace {

#ifdef _WIN32
int create_pipe_fds(int fds[2]) {
    return _pipe(fds, 4096, _O_BINARY);
}

int close_fd(int fd) {
    return _close(fd);
}

ssize_t write_fd(int fd, const char* data, size_t len) {
    return _write(fd, data, static_cast<unsigned int>(len));
}
#else
int create_pipe_fds(int fds[2]) {
    return ::pipe(fds);
}

int close_fd(int fd) {
    return ::close(fd);
}

ssize_t write_fd(int fd, const char* data, size_t len) {
    return ::write(fd, data, len);
}
#endif

std::string frame(std::string_view payload) {
    std::string out;
    out.reserve(payload.size() + 32);
    out.append("Content-Length: ");
    out.append(std::to_string(payload.size()));
    out.append("\r\n\r\n");
    out.append(payload);
    return out;
}

task<std::pair<std::optional<std::string>, std::optional<std::string>>>
    read_two_messages(StreamTransport& transport) {
    auto first = co_await transport.read_message();
    auto second = co_await transport.read_message();
    event_loop::current().stop();
    co_return std::pair{std::move(first), std::move(second)};
}

}  // namespace

TEST_SUITE(jsonrpc_transport) {

TEST_CASE(stream_transport_reads_back_to_back_messages) {
    event_loop loop;

    int fds[2] = {-1, -1};
    ASSERT_EQ(create_pipe_fds(fds), 0);

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

};  // TEST_SUITE(jsonrpc_transport)

}  // namespace eventide::jsonrpc
