#include <algorithm>
#include <chrono>
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

using Fixture = test::PeerFixture<test::JsonAdapter>;
using Context = Fixture::Context;
using test::AddParams;
using test::AddResult;
using test::NoteParams;
using namespace std::chrono_literals;

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
    auto received = test::request<test::JsonAdapter>(1, "test/add", AddParams{.a = 1, .b = 2});
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
    remote.send(test::request<test::JsonAdapter>(1, "test/add", AddParams{.a = 1, .b = 2}));
    remote.send(test::notification<test::JsonAdapter>("unknown/note", NoteParams{}));
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
    remote.send(test::notification<test::JsonAdapter>("unknown/note", NoteParams{}));
    remote.end_input();

    auto [ran] = run(peer.run());
    EXPECT(ran.has_value());
    EXPECT(has(LogLevel::warn, "unhandled notification: unknown/note"));
}

// The remote answers once the request has timed out: nothing awaits the
// answer, which is no surprise, so it is no warning.
ZEST_CASE(answer_after_the_timeout_is_debug) {
    log_from(LogLevel::debug);
    event ended;
    auto ask = [&]() -> task<> {
        co_await peer.send_request(AddParams{}, {.timeout = 10ms});
        ended.set();
    };
    auto respond = [&]() -> task<> {
        co_await next();
        co_await next();
        co_await ended.wait();
        remote.send(test::response<test::JsonAdapter>(1, AddResult{.sum = 1}));
        remote.end_input();
    };

    auto [ran, asked, responded] = run(peer.run(), ask(), respond());
    EXPECT(ran.has_value());
    EXPECT(has(LogLevel::debug, "late response for id=1"));
    for(const auto& entry: entries) {
        ZEST_CONTEXT("entry: {}", entry.text);
        EXPECT(entry.level < LogLevel::warn);
    }
}

// An id the peer never gave a request of its own. The log shows a number as
// it is and a string in quotes, so that 7 and "7" read apart.
ZEST_CASE(answer_to_an_unknown_id_is_a_warning) {
    log_from(LogLevel::warn);
    remote.send(test::response<test::JsonAdapter>(7, AddResult{}));
    remote.send(test::response<test::JsonAdapter>("7", AddResult{}));
    remote.end_input();

    auto [ran] = run(peer.run());
    EXPECT(ran.has_value());
    EXPECT(has(LogLevel::warn, "orphan response for id=7"));
    EXPECT(has(LogLevel::warn, R"(orphan response for id="7")"));
}

// run() is cancelled once the request is read and before its handler starts:
// the request is answered RequestCancelled all the same, an answer the output
// closed with run() then drops.
ZEST_CASE(request_cancelled_with_run_before_its_handler_starts_is_answered) {
    log_from(LogLevel::debug);
    bool ran = false;
    peer.on_request([&](Context&, const AddParams&) -> RequestResult<AddParams> {
        ran = true;
        co_return AddResult{};
    });
    remote.send(test::request<test::JsonAdapter>(1, "test/add", AddParams{}));
    cancellation_source source;
    auto stop = [&]() -> task<> {
        source.cancel();
        co_return;
    };

    auto [stopped_run, stopped] = run(with_token(peer.run(), source.token()), stop());
    EXPECT(stopped_run.is_cancelled());
    EXPECT(!ran);
    EXPECT(has(LogLevel::error, "error response: request cancelled"));
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
