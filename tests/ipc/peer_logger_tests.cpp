#include <string>
#include <utility>
#include <vector>

#include "peer_test_types.h"
#include "eventide/ipc/logger.h"
#include "eventide/zest/zest.h"
#include "eventide/async/async.h"

namespace eventide::ipc {

struct LogEntry {
    LogLevel level;
    std::string message;
};

using LogEntries = std::vector<LogEntry>;

TEST_SUITE(ipc_peer_logger) {

TEST_CASE(trace_logs_traffic) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":1,"method":"test/add","params":{"a":1,"b":2}})",
    });

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    LogEntries logs;
    peer.set_logger(
        [&](LogLevel level, std::string msg) { logs.push_back({level, std::move(msg)}); },
        LogLevel::trace);

    peer.on_request([&](RequestContext&, const AddParams& p) -> RequestResult<AddParams> {
        co_return AddResult{.sum = p.a + p.b};
    });

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    // Should have trace logs for recv and send
    bool has_recv = false;
    bool has_send = false;
    for(const auto& entry: logs) {
        if(entry.level == LogLevel::trace && entry.message.starts_with("recv:")) {
            has_recv = true;
        }
        if(entry.level == LogLevel::trace && entry.message.starts_with("send:")) {
            has_send = true;
        }
    }
    EXPECT_TRUE(has_recv);
    EXPECT_TRUE(has_send);
}

TEST_CASE(level_filtering) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":1,"method":"test/add","params":{"a":1,"b":2}})",
    });

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    LogEntries logs;
    peer.set_logger(
        [&](LogLevel level, std::string msg) { logs.push_back({level, std::move(msg)}); },
        LogLevel::warn);

    peer.on_request([&](RequestContext&, const AddParams& p) -> RequestResult<AddParams> {
        co_return AddResult{.sum = p.a + p.b};
    });

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    // With min_level=warn, no trace/debug/info logs should appear
    for(const auto& entry: logs) {
        EXPECT_TRUE(entry.level >= LogLevel::warn);
    }
}

TEST_CASE(deser_failure_warns) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","method":"test/note","params":{"text":12345}})",
    });

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    LogEntries logs;
    peer.set_logger(
        [&](LogLevel level, std::string msg) { logs.push_back({level, std::move(msg)}); },
        LogLevel::warn);

    bool called = false;
    peer.on_notification([&](const NoteParams&) { called = true; });

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);
    EXPECT_FALSE(called);

    bool has_deser_warn = false;
    for(const auto& entry: logs) {
        if(entry.level == LogLevel::warn &&
           entry.message.find("deserialization failed") != std::string::npos) {
            has_deser_warn = true;
        }
    }
    EXPECT_TRUE(has_deser_warn);
}

TEST_CASE(unhandled_notification_warns) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","method":"unknown/method","params":{}})",
    });

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    LogEntries logs;
    peer.set_logger(
        [&](LogLevel level, std::string msg) { logs.push_back({level, std::move(msg)}); },
        LogLevel::warn);

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    bool has_unhandled_warn = false;
    for(const auto& entry: logs) {
        if(entry.level == LogLevel::warn &&
           entry.message.find("unhandled notification") != std::string::npos) {
            has_unhandled_warn = true;
        }
    }
    EXPECT_TRUE(has_unhandled_warn);
}

TEST_CASE(no_logger_no_crash) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":1,"method":"test/add","params":{"a":1,"b":2}})",
        R"({"jsonrpc":"2.0","method":"test/note","params":{"text":"hi"}})",
    });
    auto* tp = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    peer.on_request([&](RequestContext&, const AddParams& p) -> RequestResult<AddParams> {
        co_return AddResult{.sum = p.a + p.b};
    });
    peer.on_notification([&](const NoteParams&) {});

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);
    EXPECT_EQ(tp->outgoing().size(), 1U);
}

TEST_CASE(read_loop_lifecycle) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{});

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    LogEntries logs;
    peer.set_logger(
        [&](LogLevel level, std::string msg) { logs.push_back({level, std::move(msg)}); },
        LogLevel::info);

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    bool has_started = false;
    bool has_ended = false;
    for(const auto& entry: logs) {
        if(entry.level == LogLevel::info && entry.message.find("started") != std::string::npos) {
            has_started = true;
        }
        if(entry.level == LogLevel::info && entry.message.find("ended") != std::string::npos) {
            has_ended = true;
        }
    }
    EXPECT_TRUE(has_started);
    EXPECT_TRUE(has_ended);
}

};  // TEST_SUITE(ipc_peer_logger)

}  // namespace eventide::ipc
