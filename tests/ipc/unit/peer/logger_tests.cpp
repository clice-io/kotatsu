#include <algorithm>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "ipc/harness/codec_json.h"
#include "ipc/harness/peer_fixture.h"
#include "kota/ipc/codec/json.h"
#include "kota/ipc/logger.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"

namespace kota::ipc {

namespace {

using Fixture = test::PeerFixture<test::JSONAdapter>;
using test::AddParams;
using test::NoteParams;

struct LogEntry {
    LogLevel level;
    std::string text;
};

/// What the peer logs at `level` and above.
struct Logged : Fixture {
    std::vector<LogEntry> entries;

    void log_from(LogLevel level) {
        peer.set_logger(
            [this](LogLevel at, std::string text) {
                entries.push_back({.level = at, .text = std::move(text)});
            },
            level);
    }

    /// An entry at `level` contains `text`; a failed check lists the entries.
    zest::Match has(LogLevel level, const std::string& text) const {
        bool held = std::ranges::any_of(entries, [&](const LogEntry& entry) {
            return entry.level == level && entry.text.find(text) != std::string::npos;
        });
        return zest::Match{.held = held, .explain = [this, level, text] {
                               return std::format("level: {}\ntext: {}\nentries: {}",
                                                  zest::pretty_dump(level),
                                                  text,
                                                  zest::pretty_dump(entries));
                           }};
    }
};

ZEST_SUITE(ipc_peer_logger, Logged) {

ZEST_CASE(trace_shows_each_message_read_and_written) {
    log_from(LogLevel::trace);
    serve_add();
    auto received = test::request<test::JSONAdapter>(1, "test/add", AddParams{.a = 1, .b = 2});
    remote.send(received);
    remote.end_input();

    auto [ran] = run(peer.run());
    EXPECT(ran.has_value());
    auto sent = remote.drain();
    ASSERT(sent.size() == 1U);
    EXPECT(has(LogLevel::trace, "recv: " + received));
    EXPECT(has(LogLevel::trace, "send: " + sent[0]));
}

ZEST_CASE(entries_below_the_level_are_left_out) {
    log_from(LogLevel::warn);
    serve_add();
    remote.send(test::request<test::JSONAdapter>(1, "test/add", AddParams{.a = 1, .b = 2}));
    remote.send(test::notification<test::JSONAdapter>("unknown/note", NoteParams{}));
    remote.end_input();

    auto [ran] = run(peer.run());
    EXPECT(ran.has_value());
    ASSERT(!entries.empty());
    for(const auto& entry: entries) {
        ZEST_CONTEXT("entry: {}", entry.text);
        EXPECT(entry.level >= LogLevel::warn);
    }
}

ZEST_CASE(params_that_do_not_decode_are_a_warning) {
    log_from(LogLevel::warn);
    peer.on_notification([](const NoteParams&) {});
    serve_add();
    remote.send(R"({"jsonrpc":"2.0","method":"test/note","params":{"text":12345}})");
    remote.send(R"({"jsonrpc":"2.0","id":1,"method":"test/add","params":{"a":"one"}})");
    remote.end_input();

    auto [ran] = run(peer.run());
    EXPECT(ran.has_value());
    EXPECT(has(LogLevel::warn, "notification params deserialization failed"));
    EXPECT(has(LogLevel::warn, "request 'test/add' params deserialization failed"));
}

ZEST_CASE(unhandled_notification_is_a_warning) {
    log_from(LogLevel::warn);
    remote.send(test::notification<test::JSONAdapter>("unknown/note", NoteParams{}));
    remote.end_input();

    auto [ran] = run(peer.run());
    EXPECT(ran.has_value());
    EXPECT(has(LogLevel::warn, "unhandled notification: unknown/note"));
}

ZEST_CASE(notification_that_is_not_jsonrpc_2_0_is_dropped_with_a_warning) {
    log_from(LogLevel::warn);
    bool called = false;
    peer.on_notification([&](const NoteParams&) { called = true; });
    remote.send(R"({"jsonrpc":"1.0","method":"test/note","params":{"text":"x"}})");
    remote.end_input();

    auto [ran] = run(peer.run());
    EXPECT(ran.has_value());
    EXPECT(!called);
    EXPECT(written().empty());
    EXPECT(has(LogLevel::warn, "dropped a notification"));
}

// A string id is logged quoted, so that it stays apart from a number.
ZEST_CASE(orphan_response_logs_its_id_as_written) {
    log_from(LogLevel::warn);
    remote.send(test::response<test::JSONAdapter>(protocol::RequestID("7"), test::AddResult{}));
    remote.send(test::response<test::JSONAdapter>(protocol::RequestID(7), test::AddResult{}));
    remote.end_input();

    auto [ran] = run(peer.run());
    EXPECT(ran.has_value());
    EXPECT(has(LogLevel::warn, R"(orphan response for id="7")"));
    EXPECT(has(LogLevel::warn, "orphan response for id=7"));
}

// close() fails the handler's own request before it cancels the handler,
// whose token then finds nothing left to tell the remote.
ZEST_CASE(close_tells_the_remote_nothing_of_a_handlers_request) {
    log_from(LogLevel::error);
    peer.on_request([](Fixture::Context& context,
                       const AddParams& params) -> RequestResult<AddParams> {
        co_return co_await context
            ->send_request<test::AddResult>("client/add", params, {.token = context.cancellation})
            .or_fail();
    });
    remote.send(test::request<test::JSONAdapter>(1, "test/add", AddParams{}));
    auto closer = [&]() -> task<> {
        co_await next();
        peer.close();
    };

    auto [ran, closed] = run(peer.run(), closer());
    EXPECT(ran.has_value());
    EXPECT(has(LogLevel::error, "failing 1 pending request(s): peer closed"));
    EXPECT(std::ranges::none_of(entries, [](const LogEntry& entry) {
        return entry.text.find("$/cancelRequest") != std::string::npos;
    }));
    ASSERT(written().size() == 1U);
    EXPECT(written()[0].method == "client/add");
}

ZEST_CASE(run_logs_where_its_read_loop_starts_and_ends) {
    log_from(LogLevel::info);
    remote.end_input();

    auto [ran] = run(peer.run());
    EXPECT(ran.has_value());
    EXPECT(has(LogLevel::info, "read loop started"));
    EXPECT(has(LogLevel::info, "read loop ended"));
}

};  // ZEST_SUITE(ipc_peer_logger)

}  // namespace

}  // namespace kota::ipc
