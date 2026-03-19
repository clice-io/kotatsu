#include <format>
#include <string>
#include <utility>
#include <vector>

#include "../peer_test_types.h"
#include "eventide/zest/zest.h"
#include "eventide/async/async.h"
#include "eventide/ipc/lsp/progress.h"

namespace eventide::ipc {

using lsp::ProgressReporter;

TEST_SUITE(ipc_progress) {

TEST_CASE(create_sends_request) {
    auto hook = [](std::string_view payload, ScriptedTransport& t) {
        if(payload.find(R"("method":"window/workDoneProgress/create")") != std::string_view::npos) {
            t.push_incoming(R"({"jsonrpc":"2.0","id":1,"result":null})");
        }
    };

    auto transport = std::make_unique<ScriptedTransport>(std::vector<std::string>{},
                                                         ScriptedTransport::WriteHook(hook));
    auto* tp = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    Result<void> create_result = outcome_error(Error("not run"));

    auto requester = [&]() -> task<> {
        ProgressReporter reporter(peer, protocol::ProgressToken(1));
        auto r = co_await reporter.create();
        create_result =
            r.has_value() ? Result<void>(outcome_value()) : Result<void>(outcome_error(r.error()));
        tp->close();
    };

    auto req_task = requester();
    loop.schedule(peer.run());
    loop.schedule(req_task);
    EXPECT_EQ(loop.run(), 0);

    EXPECT_TRUE(create_result.has_value());
    ASSERT_GE(tp->outgoing().size(), 1U);
    EXPECT_TRUE(tp->outgoing()[0].find("window/workDoneProgress/create") != std::string::npos);
}

TEST_CASE(begin_report_end) {
    auto hook = [](std::string_view payload, ScriptedTransport& t) {
        if(payload.find(R"("method":"window/workDoneProgress/create")") != std::string_view::npos) {
            t.push_incoming(R"({"jsonrpc":"2.0","id":1,"result":null})");
        }
    };

    auto transport = std::make_unique<ScriptedTransport>(std::vector<std::string>{},
                                                         ScriptedTransport::WriteHook(hook));
    auto* tp = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    auto requester = [&]() -> task<> {
        ProgressReporter reporter(peer, protocol::ProgressToken(42));
        auto r = co_await reporter.create();
        EXPECT_TRUE(r.has_value());
        reporter.begin("Indexing", "Starting...", protocol::uinteger(0));
        reporter.report("50% done", protocol::uinteger(50));
        reporter.end("Complete");
        tp->close();
    };

    auto req_task = requester();
    loop.schedule(peer.run());
    loop.schedule(req_task);
    EXPECT_EQ(loop.run(), 0);

    // outgoing[0] = create request, outgoing[1..3] = progress notifications
    ASSERT_GE(tp->outgoing().size(), 4U);

    EXPECT_TRUE(tp->outgoing()[1].find(R"("method":"$/progress")") != std::string::npos);
    EXPECT_TRUE(tp->outgoing()[1].find(R"("kind":"begin")") != std::string::npos);
    EXPECT_TRUE(tp->outgoing()[1].find(R"("title":"Indexing")") != std::string::npos);

    EXPECT_TRUE(tp->outgoing()[2].find(R"("method":"$/progress")") != std::string::npos);
    EXPECT_TRUE(tp->outgoing()[2].find(R"("kind":"report")") != std::string::npos);

    EXPECT_TRUE(tp->outgoing()[3].find(R"("method":"$/progress")") != std::string::npos);
    EXPECT_TRUE(tp->outgoing()[3].find(R"("kind":"end")") != std::string::npos);
}

TEST_CASE(string_token) {
    auto hook = [](std::string_view payload, ScriptedTransport& t) {
        if(payload.find(R"("method":"window/workDoneProgress/create")") != std::string_view::npos) {
            t.push_incoming(R"({"jsonrpc":"2.0","id":1,"result":null})");
        }
    };

    auto transport = std::make_unique<ScriptedTransport>(std::vector<std::string>{},
                                                         ScriptedTransport::WriteHook(hook));
    auto* tp = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    auto requester = [&]() -> task<> {
        ProgressReporter reporter(peer, protocol::ProgressToken(std::string("my-token")));
        auto r = co_await reporter.create();
        EXPECT_TRUE(r.has_value());
        reporter.begin("Building");
        reporter.end();
        tp->close();
    };

    auto req_task = requester();
    loop.schedule(peer.run());
    loop.schedule(req_task);
    EXPECT_EQ(loop.run(), 0);

    ASSERT_GE(tp->outgoing().size(), 3U);
    EXPECT_TRUE(tp->outgoing()[0].find(R"("my-token")") != std::string::npos);
}

TEST_CASE(create_failure) {
    auto hook = [](std::string_view payload, ScriptedTransport& t) {
        if(payload.find(R"("method":"window/workDoneProgress/create")") != std::string_view::npos) {
            t.push_incoming(
                R"({"jsonrpc":"2.0","id":1,"error":{"code":-32600,"message":"not supported"}})");
        }
    };

    auto transport = std::make_unique<ScriptedTransport>(std::vector<std::string>{},
                                                         ScriptedTransport::WriteHook(hook));
    auto* tp = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    Result<void> create_result = outcome_error(Error("not run"));

    auto requester = [&]() -> task<> {
        ProgressReporter reporter(peer, protocol::ProgressToken(1));
        auto r = co_await reporter.create();
        create_result =
            r.has_value() ? Result<void>(outcome_value()) : Result<void>(outcome_error(r.error()));
        tp->close();
    };

    auto req_task = requester();
    loop.schedule(peer.run());
    loop.schedule(req_task);
    EXPECT_EQ(loop.run(), 0);

    EXPECT_FALSE(create_result.has_value());
    EXPECT_EQ(create_result.error().message, "not supported");
}

};  // TEST_SUITE(ipc_progress)

}  // namespace eventide::ipc
