#include <cstddef>
#include <expected>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "async/harness/os.h"
#include "ipc/harness/frames.h"
#include "kota/ipc/transport.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::ipc {

namespace {

/// Both ends of an anonymous pipe, opened as kota pipes.
struct Ends {
    pipe reader;
    pipe writer;
};

result<Ends> pipe_ends(event_loop& loop) {
    int fds[2] = {-1, -1};
    if(test::create_pipe(fds) != 0) {
        return outcome_error(error::io_error);
    }
    auto reader = pipe::open(fds[0], {}, loop);
    auto writer = pipe::open(fds[1], {}, loop);
    if(!reader || !writer) {
        return outcome_error(error::io_error);
    }
    return Ends{.reader = std::move(*reader), .writer = std::move(*writer)};
}

/// A transport reading a pipe whose write end the test holds as a raw
/// descriptor, so that it can write from a thread.
struct Feed {
    std::unique_ptr<StreamTransport> transport;
    int writer = -1;
};

std::optional<Feed> feed(event_loop& loop, std::size_t max_payload = default_max_payload) {
    int fds[2] = {-1, -1};
    if(test::create_pipe(fds) != 0) {
        return std::nullopt;
    }
    auto reader = pipe::open(fds[0], {}, loop);
    if(!reader) {
        test::close_fd(fds[1]);
        return std::nullopt;
    }
    return Feed{
        .transport = std::make_unique<StreamTransport>(stream(std::move(*reader)), max_payload),
        .writer = fds[1],
    };
}

struct StreamFixture : zest::LoopFixture {
    /// What one read_message() returns from a pipe that holds `text` and was
    /// closed after it.
    std::expected<std::string, ReadError>
        read_after(std::string_view text, std::size_t max_payload = default_max_payload) {
        auto input = feed(loop, max_payload);
        ZASSERT(input.has_value());
        auto written = test::write_fd(input->writer, text.data(), text.size());
        test::close_fd(input->writer);
        ZEXPECT(written == static_cast<ssize_t>(text.size()));
        auto [read] = run(input->transport->read_message());
        // Reports the failure: nothing here cancels the read.
        ZEXPECT(!read.is_cancelled());
        if(!read.has_value()) {
            return std::unexpected(read.has_error() ? std::move(read).error() : ReadError{});
        }
        return std::move(*read);
    }
};

ZEST_SUITE(ipc_transport_stream, StreamFixture) {

ZEST_CASE(messages_in_one_write_read_in_order) {
    auto input = feed(loop);
    ZASSERT(input.has_value());
    std::vector<std::string> sent;
    std::string frames;
    for(int i = 0; i < 10; ++i) {
        sent.push_back(std::format(R"({{"i":{}}})", i));
        frames += test::framed(sent.back());
    }
    ZASSERT(test::write_fd(input->writer, frames.data(), frames.size()) ==
            static_cast<ssize_t>(frames.size()));
    ZASSERT(test::close_fd(input->writer) == 0);
    auto read_all = [&]() -> task<std::vector<std::string>> {
        std::vector<std::string> messages;
        while(auto message = co_await input->transport->read_message()) {
            messages.push_back(std::move(*message));
        }
        co_return messages;
    };

    auto [read] = run(read_all());
    ZASSERT(read.has_value());
    ZEXPECT(*read == sent);
}

// The writer's pieces split the header name, the blank line and the
// payload; however the pipe hands them over, each message reads whole.
ZEST_CASE(messages_split_across_writes_read_whole) {
    auto ends = pipe_ends(loop);
    ZASSERT(ends.has_value());
    StreamTransport transport(stream(std::move(ends->reader)));
    const std::vector<std::string> sent{"first", R"({"second":2})"};
    const auto frames = test::framed(sent[0]) + test::framed(sent[1]);
    auto write_in_pieces = [&]() -> task<void, error> {
        for(std::size_t at = 0; at < frames.size(); at += 3) {
            co_await ends->writer.write(std::string_view(frames).substr(at, 3)).or_fail();
        }
        ends->writer = pipe();
    };
    auto read_all = [&]() -> task<std::vector<std::string>> {
        std::vector<std::string> messages;
        while(auto message = co_await transport.read_message()) {
            messages.push_back(std::move(*message));
        }
        co_return messages;
    };

    auto [wrote, read] = run(write_in_pieces(), read_all());
    ZEXPECT(wrote.has_value());
    ZASSERT(read.has_value());
    ZEXPECT(*read == sent);
}

ZEST_CASE(empty_payload_reads_as_empty) {
    ZEXPECT(read_after("Content-Length: 0\r\n\r\n") == std::string());
}

// Windows pipes hold 4 KB, so the 10 KB frame is written from a thread while
// the loop reads; the header is small and the chunk holding it is not.
ZEST_CASE(large_payload_reads_whole) {
    auto input = feed(loop);
    ZASSERT(input.has_value());
    const std::string payload(10 * 1024, 'x');
    const auto data = test::framed(payload);
    ssize_t written = 0;
    std::thread writer([&] {
        written = test::write_fd(input->writer, data.data(), data.size());
        test::close_fd(input->writer);
    });

    auto [read] = run(input->transport->read_message());
    writer.join();
    ZEXPECT(written == static_cast<ssize_t>(data.size()));
    ZASSERT(read.has_value());
    ZEXPECT(read->size() == payload.size());
}

// The input ends between messages or inside one alike.
ZEST_CASE(end_of_input_is_closed) {
    for(std::string_view text: {"", "Content-Length: 10\r\n", "Content-Length: 100\r\n\r\nhello"}) {
        ZEST_CONTEXT("input: {} bytes", text.size());
        auto read = read_after(text);
        ZASSERT(!read.has_value());
        ZEXPECT(read.error().kind == ReadError::Kind::Closed);
    }
}

// How headers are read is ipc_framing's; here a header that cannot be read
// reaches the reader through a pipe.
ZEST_CASE(unreadable_header_fails) {
    auto read = read_after("Content-Length: 5x\r\n\r\nhello");
    ZASSERT(!read.has_value());
    ZEXPECT(read.error().kind == ReadError::Kind::Malformed);
}

// A frame over the transport's limit is skipped, and the next one reads.
ZEST_CASE(oversized_message_is_skipped_and_reading_goes_on) {
    auto input = feed(loop, 8);
    ZASSERT(input.has_value());
    const auto data = test::framed("0123456789") + test::framed("next");
    ZASSERT(test::write_fd(input->writer, data.data(), data.size()) ==
            static_cast<ssize_t>(data.size()));
    ZASSERT(test::close_fd(input->writer) == 0);

    auto read_twice = [&]() -> task<std::pair<ReadError, std::string>, ReadError> {
        auto skipped = co_await input->transport->read_message();
        auto next = co_await input->transport->read_message().or_fail();
        co_return std::pair{skipped.has_error() ? std::move(skipped).error() : ReadError{},
                            std::move(next)};
    };

    auto [read] = run(read_twice());
    ZASSERT(read.has_value());
    auto& [skipped, next] = *read;
    ZEXPECT(skipped.kind == ReadError::Kind::Oversized);
    ZEXPECT(skipped.size == 10U);
    ZEXPECT(skipped.prefix == "0123456789");
    ZEXPECT(next == "next");
}

// The limit it reads with says nothing of what the remote reads: what it
// sends has no limit until it is told one.
ZEST_CASE(remote_max_payload_is_unlimited_until_set) {
    auto input = feed(loop, 8);
    ZASSERT(input.has_value());
    auto& transport = *input->transport;
    ZEXPECT(transport.remote_max_payload() == std::numeric_limits<std::size_t>::max());
    transport.set_remote_max_payload(8);
    ZEXPECT(transport.remote_max_payload() == 8U);
    test::close_fd(input->writer);
}

ZEST_CASE(close_wakes_a_pending_read) {
    auto ends = pipe_ends(loop);
    ZASSERT(ends.has_value());
    StreamTransport transport(stream(std::move(ends->reader)), stream(std::move(ends->writer)));
    auto closer = [&]() -> task<bool> {
        co_return transport.close().has_value();
    };

    auto [read, closed] = run(transport.read_message(), closer());
    ZASSERT(read.has_error());
    ZEXPECT(read.error().kind == ReadError::Kind::Closed);
    ZASSERT(closed.has_value());
    ZEXPECT(*closed);
}

ZEST_CASE(write_frames_the_payload) {
    auto ends = pipe_ends(loop);
    ZASSERT(ends.has_value());
    StreamTransport transport(stream(std::move(ends->writer)));
    auto send = [&]() -> task<void, Error> {
        co_await transport.write_message("hello").or_fail();
        transport.close();
    };
    auto read_all = [&]() -> task<std::string> {
        std::string text;
        while(auto chunk = co_await ends->reader.read()) {
            text += *chunk;
        }
        co_return text;
    };

    auto [sent, read] = run(send(), read_all());
    ZEXPECT(sent.has_value());
    ZASSERT(read.has_value());
    ZEXPECT(*read == "Content-Length: 5\r\n\r\nhello");
}

ZEST_CASE(write_messages_frames_each_in_order) {
    auto ends = pipe_ends(loop);
    ZASSERT(ends.has_value());
    StreamTransport transport(stream(std::move(ends->writer)));
    const std::vector<std::string> payloads = {"first", "", std::string(100'000, 'x'), "last"};
    auto send = [&]() -> task<void, Error> {
        co_await transport.write_messages(payloads).or_fail();
        transport.close();
    };

    auto [sent, read] = run(send(), ends->reader.read_to_end());
    ZEXPECT(sent.has_value());
    ZASSERT(read.has_value());
    ZEXPECT(*read == test::framed(payloads[0]) + test::framed(payloads[1]) +
                         test::framed(payloads[2]) + test::framed(payloads[3]));
}

// A pipe's read end is not writable, so the write fails without a signal.
ZEST_CASE(write_to_the_read_end_fails) {
    auto ends = pipe_ends(loop);
    ZASSERT(ends.has_value());
    StreamTransport transport(stream(std::move(ends->writer)), stream(std::move(ends->reader)));

    auto [written] = run(transport.write_message("hello"));
    ZASSERT(written.has_error());
    ZEXPECT(written.error().message == error::broken_pipe.message());
}

ZEST_CASE(connect_tcp_exchanges_messages) {
    auto listener = tcp::listen("127.0.0.1", 0, {}, loop);
    ZASSERT(listener.has_value());
    auto name = listener->getsockname();
    ZASSERT(name.has_value());
    const int port = name->port;
    auto serve = [&]() -> task<std::optional<std::string>> {
        auto connection = co_await listener->accept();
        if(!connection) {
            co_return std::nullopt;
        }
        StreamTransport server(stream(std::move(*connection)));
        auto request = co_await server.read_message();
        co_await server.write_message("pong");
        co_return request ? std::optional(std::move(*request)) : std::nullopt;
    };
    auto ask = [&]() -> task<std::optional<std::string>, Error> {
        auto client = co_await StreamTransport::connect_tcp("127.0.0.1", port, loop).or_fail();
        co_await client->write_message("ping").or_fail();
        auto answer = co_await client->read_message();
        co_return answer ? std::optional(std::move(*answer)) : std::nullopt;
    };

    auto [served, asked] = run(serve(), ask());
    ZASSERT(served.has_value());
    ZEXPECT(*served == "ping");
    ZASSERT(asked.has_value());
    ZEXPECT(*asked == "pong");
}

// One socket both ways: close_output() shuts its write side down once what
// was written has gone out. The remote reads it, then the end of its input,
// and can still send; the transport reads what it sends.
ZEST_CASE(close_output_on_a_shared_socket_ends_the_remote_input_and_keeps_reading) {
    auto listener = tcp::listen("127.0.0.1", 0, {}, loop);
    ZASSERT(listener.has_value());
    auto name = listener->getsockname();
    ZASSERT(name.has_value());
    auto [accepted, connected] =
        run(listener->accept(), StreamTransport::connect_tcp("127.0.0.1", name->port, loop));
    ZASSERT(accepted.has_value());
    ZASSERT(connected.has_value());
    auto& transport = **connected;
    auto remote = [&]() -> task<std::string, error> {
        auto received = co_await accepted->read_to_end().or_fail();
        auto answer = test::framed("after");
        co_await accepted->write(std::span<const char>(answer.data(), answer.size())).or_fail();
        co_return received;
    };
    auto local = [&]() -> task<std::string, Error> {
        co_await transport.write_message("before").or_fail();
        co_await transport.close_output().or_fail();
        auto read = co_await transport.read_message();
        if(!read) {
            co_await fail(Error(read.error().message));
        }
        co_return std::move(*read);
    };

    auto [received, read] = run(remote(), local());
    ZASSERT(received.has_value());
    ZEXPECT(*received == test::framed("before"));
    ZASSERT(read.has_value());
    ZEXPECT(*read == "after");
}

ZEST_CASE(connect_tcp_to_a_bad_address_fails) {
    auto [connected] = run(StreamTransport::connect_tcp("not-an-address", 80, loop));
    ZASSERT(connected.has_error());
    ZEXPECT(connected.error().message == error::invalid_argument.message());
}

};  // ZEST_SUITE(ipc_transport_stream)

}  // namespace

}  // namespace kota::ipc
