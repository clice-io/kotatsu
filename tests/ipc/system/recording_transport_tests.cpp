#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "async/harness/os.h"
#include "ipc/harness/memory_transport.h"
#include "kota/ipc/recording_transport.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"
#include "kota/codec/json/json.h"

namespace kota::ipc {

namespace {

/// One line of a recording.
struct Record {
    std::int64_t ts = 0;
    std::string msg;
};

/// A RecordingTransport over a Remote's transport, writing to a file in a
/// directory of its own.
struct Recording : test::LoopFixture {
    test::TempDir dir;
    std::string path = dir.file("trace.jsonl");
    test::Remote remote;
    RecordingTransport transport{remote.transport(), path};

    /// The recording's lines, each read back; a line that does not read
    /// fails the test.
    std::vector<Record> records() const {
        std::vector<Record> lines;
        auto text = test::read_file(path);
        std::string_view rest = text;
        while(!rest.empty()) {
            auto end = rest.find('\n');
            auto line = rest.substr(0, end);
            rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
            auto record = codec::json::from_string<Record>(line);
            ZEST_CONTEXT("line: {}", line);
            EXPECT(record.has_value());
            if(record) {
                lines.push_back(std::move(*record));
            }
        }
        return lines;
    }
};

ZEST_SUITE(ipc_recording_transport, Recording) {

// The second message holds every character the recording escapes.
ZEST_CASE(read_messages_are_recorded_and_passed_on) {
    const std::vector<std::string> sent{
        R"({"jsonrpc":"2.0","method":"exit"})",
        "quote\" backslash\\ newline\n return\r tab\t backspace\b feed\f control\x01",
    };
    for(const auto& message: sent) {
        remote.send(message);
    }
    remote.end_input();
    auto read_all = [&]() -> task<std::vector<std::string>> {
        std::vector<std::string> messages;
        while(auto message = co_await transport.read_message()) {
            messages.push_back(std::move(*message));
        }
        co_return messages;
    };

    auto [read] = run(read_all());
    ASSERT(read.has_value());
    EXPECT(*read == sent);
    auto lines = records();
    ASSERT(lines.size() == 2U);
    for(std::size_t i = 0; i < lines.size(); ++i) {
        ZEST_CONTEXT("record {}", i);
        EXPECT(lines[i].msg == sent[i]);
        EXPECT(lines[i].ts >= 0);
    }
}

ZEST_CASE(written_messages_are_passed_on_unrecorded) {
    auto [written] = run(transport.write_message("hello"));
    EXPECT(written.has_value());
    EXPECT(remote.drain() == std::vector<std::string>{"hello"});
    EXPECT(test::read_file(path).empty());
}

ZEST_CASE(close_reaches_the_inner_transport) {
    EXPECT(transport.close().has_value());
    EXPECT(remote.closed());
}

ZEST_CASE(close_output_reaches_the_inner_transport) {
    auto [closed] = run(transport.close_output());
    EXPECT(closed.has_value());
    EXPECT(remote.output_ended());
    EXPECT(!remote.closed());
}

ZEST_CASE(unreadable_message_is_passed_on_unrecorded) {
    remote.send_unreadable(ReadError{.kind = ReadError::Kind::Oversized, .message = "too large"});

    auto [read] = run(transport.read_message());
    ASSERT(read.has_error());
    EXPECT(read.error().kind == ReadError::Kind::Oversized);
    EXPECT(test::read_file(path).empty());
}

// A file that cannot be opened loses the recording, not the messages.
ZEST_CASE(unopenable_file_still_passes_messages_on) {
    test::Remote other;
    auto missing = dir.file("missing/trace.jsonl");
    RecordingTransport unrecorded(other.transport(), missing);
    other.send("hello");

    auto [read] = run(unrecorded.read_message());
    ASSERT(read.has_value());
    EXPECT(*read == "hello");
    EXPECT(!std::filesystem::exists(missing));
}

ZEST_CASE(max_payload_is_the_inner_transport_limit) {
    remote.limit_payload(100);
    EXPECT(transport.max_payload() == 100U);
}

ZEST_CASE(write_failure_reaches_the_caller) {
    remote.fail_writes();

    auto [written] = run(transport.write_message("hello"));
    ASSERT(written.has_error());
    EXPECT(written.error().message == "write failed");
}

};  // ZEST_SUITE(ipc_recording_transport)

}  // namespace

}  // namespace kota::ipc
