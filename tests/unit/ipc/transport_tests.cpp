#include <atomic>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "test_transport.h"
#include "../support/fd_helpers.h"
#include "kota/ipc/transport.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::ipc {

using test::create_pipe;
using test::close_fd;
using test::write_fd;

namespace {

using ReadResult = Result<std::optional<std::string>>;

task<std::pair<ReadResult, ReadResult>> read_two_messages(StreamTransport& transport) {
    auto first = co_await transport.read_message();
    auto second = co_await transport.read_message();
    event_loop::current().stop();
    co_return std::pair{std::move(first), std::move(second)};
}

/// Read one message, driving `loop` until it completes.
ReadResult read_one(StreamTransport& transport, event_loop& loop) {
    auto reader = [&]() -> task<ReadResult> {
        co_return co_await transport.read_message();
    };
    auto read_task = reader();
    loop.schedule(read_task);
    loop.run();
    return read_task.result();
}

TEST_SUITE(ipc_transport) {

TEST_CASE(consecutive_messages) {
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

    const auto& [first, second] = reader.result();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(first->has_value());
    ASSERT_TRUE(second->has_value());
    EXPECT_EQ(**first, first_payload);
    EXPECT_EQ(**second, second_payload);
}

// 6.1 Content-Length: 0 → empty string payload
TEST_CASE(empty_payload) {
    event_loop loop;

    int fds[2] = {-1, -1};
    ASSERT_EQ(create_pipe(fds), 0);

    auto input = pipe::open(fds[0], pipe::options{}, loop);
    ASSERT_TRUE(input.has_value());

    StreamTransport transport(stream(std::move(*input)));

    const std::string data = "Content-Length: 0\r\n\r\n";
    ASSERT_EQ(write_fd(fds[1], data.data(), data.size()), static_cast<ssize_t>(data.size()));
    ASSERT_EQ(close_fd(fds[1]), 0);

    auto result = read_one(transport, loop);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->has_value());
    EXPECT_TRUE((*result)->empty());
}

// 6.2 Header exceeds 8KB limit → a header-size error
TEST_CASE(header_too_large) {
    event_loop loop;

    int fds[2] = {-1, -1};
    ASSERT_EQ(create_pipe(fds), 0);

    auto input = pipe::open(fds[0], pipe::options{}, loop);
    ASSERT_TRUE(input.has_value());

    StreamTransport transport(stream(std::move(*input)));

    // Build a header that exceeds 8KB before the \r\n\r\n marker
    std::string huge_header = "Content-Length: 5\r\n";
    huge_header += "X-Padding: ";
    huge_header.append(9000, 'A');
    huge_header += "\r\n\r\nhello";

    std::atomic<bool> writer_done = false;
    std::thread writer([&] {
        EXPECT_EQ(write_fd(fds[1], huge_header.data(), huge_header.size()),
                  static_cast<ssize_t>(huge_header.size()));
        EXPECT_EQ(close_fd(fds[1]), 0);
        writer_done.store(true, std::memory_order_release);
    });

    auto result = read_one(transport, loop);

    writer.join();
    EXPECT_TRUE(writer_done.load(std::memory_order_acquire));

    ASSERT_TRUE(result.has_error());
    EXPECT_TRUE(result.error().message.contains("header exceeds"));
}

// 6.4 Incomplete header (EOF before \r\n\r\n) → an incomplete-header error
TEST_CASE(incomplete_header) {
    event_loop loop;

    int fds[2] = {-1, -1};
    ASSERT_EQ(create_pipe(fds), 0);

    auto input = pipe::open(fds[0], pipe::options{}, loop);
    ASSERT_TRUE(input.has_value());

    StreamTransport transport(stream(std::move(*input)));

    const std::string partial = "Content-Length: 10\r\n";
    ASSERT_EQ(write_fd(fds[1], partial.data(), partial.size()),
              static_cast<ssize_t>(partial.size()));
    ASSERT_EQ(close_fd(fds[1]), 0);

    auto result = read_one(transport, loop);
    ASSERT_TRUE(result.has_error());
    EXPECT_TRUE(result.error().message.contains("incomplete frame header"));
}

// 6.5 Content-Length > actual body (EOF mid-body) → a truncated-frame error
TEST_CASE(length_mismatch) {
    event_loop loop;

    int fds[2] = {-1, -1};
    ASSERT_EQ(create_pipe(fds), 0);

    auto input = pipe::open(fds[0], pipe::options{}, loop);
    ASSERT_TRUE(input.has_value());

    StreamTransport transport(stream(std::move(*input)));

    // Claim 100 bytes but only provide 5
    const std::string data = "Content-Length: 100\r\n\r\nhello";
    ASSERT_EQ(write_fd(fds[1], data.data(), data.size()), static_cast<ssize_t>(data.size()));
    ASSERT_EQ(close_fd(fds[1]), 0);

    auto result = read_one(transport, loop);
    ASSERT_TRUE(result.has_error());
    EXPECT_TRUE(result.error().message.contains("incomplete frame payload, got 5 of 100 bytes"));
}

// 6.6 A header without a parsable Content-Length → an invalid-header error
TEST_CASE(invalid_content_length) {
    event_loop loop;

    int fds[2] = {-1, -1};
    ASSERT_EQ(create_pipe(fds), 0);

    auto input = pipe::open(fds[0], pipe::options{}, loop);
    ASSERT_TRUE(input.has_value());

    StreamTransport transport(stream(std::move(*input)));

    const std::string data = "Content-Type: text\r\nX-Broken-Length: abc\r\n\r\nhello";
    ASSERT_EQ(write_fd(fds[1], data.data(), data.size()), static_cast<ssize_t>(data.size()));
    ASSERT_EQ(close_fd(fds[1]), 0);

    auto result = read_one(transport, loop);
    ASSERT_TRUE(result.has_error());
    EXPECT_TRUE(result.error().message.contains("no valid Content-Length"));
}

// Clean end-of-stream at a frame boundary is a close, not a refusal
TEST_CASE(clean_close_between_frames) {
    event_loop loop;

    int fds[2] = {-1, -1};
    ASSERT_EQ(create_pipe(fds), 0);

    auto input = pipe::open(fds[0], pipe::options{}, loop);
    ASSERT_TRUE(input.has_value());

    StreamTransport transport(stream(std::move(*input)));

    const std::string payload = R"({"i":1})";
    const auto data = frame(payload);
    ASSERT_EQ(write_fd(fds[1], data.data(), data.size()), static_cast<ssize_t>(data.size()));
    ASSERT_EQ(close_fd(fds[1]), 0);

    auto reader = read_two_messages(transport);
    loop.schedule(reader);
    loop.run();

    const auto& [first, second] = reader.result();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(first->has_value());
    EXPECT_EQ(**first, payload);
    ASSERT_TRUE(second.has_value());
    EXPECT_FALSE(second->has_value());
}

// A read aborted by close() ends the stream without an error
TEST_CASE(close_during_read) {
    event_loop loop;

    int fds[2] = {-1, -1};
    ASSERT_EQ(create_pipe(fds), 0);

    auto input = pipe::open(fds[0], pipe::options{}, loop);
    ASSERT_TRUE(input.has_value());

    StreamTransport transport(stream(std::move(*input)));

    ReadResult result = outcome_error(Error("not completed"));
    auto reader = [&]() -> task<> {
        result = co_await transport.read_message();
        event_loop::current().stop();
    };
    auto closer = [&]() -> task<> {
        co_await sleep(1, loop);
        EXPECT_TRUE(transport.close().has_value());
    };

    loop.schedule(reader());
    loop.schedule(closer());
    loop.run();

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->has_value());
}

// 6.7 Content-Length above max_payload_bytes → an oversized-payload error
TEST_CASE(payload_too_large) {
    event_loop loop;

    int fds[2] = {-1, -1};
    ASSERT_EQ(create_pipe(fds), 0);

    auto input = pipe::open(fds[0], pipe::options{}, loop);
    ASSERT_TRUE(input.has_value());

    StreamTransport transport(stream(std::move(*input)));
    transport.max_payload_bytes = 8;

    // Header is valid; the announced payload alone trips the configured limit.
    const std::string data = frame(std::string(9, 'x'));
    ASSERT_EQ(write_fd(fds[1], data.data(), data.size()), static_cast<ssize_t>(data.size()));
    ASSERT_EQ(close_fd(fds[1]), 0);

    auto result = read_one(transport, loop);
    ASSERT_TRUE(result.has_error());
    EXPECT_TRUE(result.error().message.contains("exceeds the 8 byte limit"));
}

// 6.6 Rapid sequential writes → all correctly read
TEST_CASE(rapid_sequential) {
    event_loop loop;

    int fds[2] = {-1, -1};
    ASSERT_EQ(create_pipe(fds), 0);

    auto input = pipe::open(fds[0], pipe::options{}, loop);
    ASSERT_TRUE(input.has_value());

    StreamTransport transport(stream(std::move(*input)));

    std::string combined;
    constexpr int count = 10;
    for(int i = 0; i < count; ++i) {
        combined += frame(R"({"i":)" + std::to_string(i) + "}");
    }

    ASSERT_EQ(write_fd(fds[1], combined.data(), combined.size()),
              static_cast<ssize_t>(combined.size()));
    ASSERT_EQ(close_fd(fds[1]), 0);

    auto reader = [&]() -> task<std::vector<std::string>> {
        std::vector<std::string> results;
        for(int i = 0; i < count; ++i) {
            auto msg = co_await transport.read_message();
            if(!msg || !msg->has_value())
                break;
            results.push_back(std::move(**msg));
        }
        co_return results;
    };

    auto read_task = reader();
    loop.schedule(read_task);
    loop.run();

    auto results = read_task.result();
    ASSERT_EQ(results.size(), static_cast<std::size_t>(count));
    for(int i = 0; i < count; ++i) {
        EXPECT_EQ(results[i], R"({"i":)" + std::to_string(i) + "}");
    }
}

// Regression: a single chunk containing header + large payload (> 8KB total)
// must not be rejected by the header size check.  The old code checked
// header.size() before searching for \r\n\r\n, which meant a chunk that
// included payload bytes beyond the header would trip the 8KB limit.
TEST_CASE(large_payload_single_chunk) {
    event_loop loop;

    int fds[2] = {-1, -1};
    ASSERT_EQ(create_pipe(fds), 0);

    auto input = pipe::open(fds[0], pipe::options{}, loop);
    ASSERT_TRUE(input.has_value());

    StreamTransport transport(stream(std::move(*input)));

    // 10KB payload — total frame is well over 8KB, but the header is tiny.
    // Write from a thread because Windows pipe buffers are small (~4KB)
    // and a synchronous write would block before the event loop starts reading.
    std::string payload(10 * 1024, 'x');
    std::string data = frame(payload);

    std::thread writer([&] {
        EXPECT_EQ(write_fd(fds[1], data.data(), data.size()), static_cast<ssize_t>(data.size()));
        EXPECT_EQ(close_fd(fds[1]), 0);
    });

    auto result = read_one(transport, loop);

    writer.join();

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->has_value());
    EXPECT_EQ((*result)->size(), payload.size());
}

};  // TEST_SUITE(ipc_transport)

}  // namespace
}  // namespace kota::ipc
