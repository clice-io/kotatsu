#include <string>
#include <utility>
#include <vector>

#include "../peer_test_types.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"
#include "kota/ipc/lsp/progress.h"

namespace kota::ipc {
namespace {

using lsp::ProgressReporter;

ZEST_SUITE(ipc_progress){

    ZEST_CASE(create_sends_request){auto hook = [](std::string_view payload, ScriptedTransport& t) {
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
EXPECT(loop.run() == 0);

EXPECT(create_result.has_value());
ASSERT(tp->outgoing().size() >= 1U);
EXPECT(zest::contains(tp->outgoing()[0], "window/workDoneProgress/create"));

}  // namespace

ZEST_CASE(begin_report_end) {
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
        EXPECT(r.has_value());
        reporter.begin("Indexing", "Starting...", protocol::uinteger(0));
        reporter.report("50% done", protocol::uinteger(50), false);
        reporter.end("Complete");
        tp->close();
    };

    auto req_task = requester();
    loop.schedule(peer.run());
    loop.schedule(req_task);
    EXPECT(loop.run() == 0);

    // outgoing[0] = create request, outgoing[1..3] = progress notifications
    ASSERT(tp->outgoing().size() >= 4U);

    // begin omits a false `cancellable`; report keeps an explicit one, which
    // there means "disable the cancel button" rather than "unchanged".
    EXPECT(
        tp->outgoing()[1] ==
        R"({"jsonrpc":"2.0","method":"$/progress","params":{"token":42,"value":{"kind":"begin","title":"Indexing","message":"Starting...","percentage":0}}})");
    EXPECT(
        tp->outgoing()[2] ==
        R"({"jsonrpc":"2.0","method":"$/progress","params":{"token":42,"value":{"kind":"report","cancellable":false,"message":"50% done","percentage":50}}})");
    EXPECT(
        tp->outgoing()[3] ==
        R"({"jsonrpc":"2.0","method":"$/progress","params":{"token":42,"value":{"kind":"end","message":"Complete"}}})");
}

ZEST_CASE(string_token) {
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
        EXPECT(r.has_value());
        reporter.begin("Building");
        reporter.end();
        tp->close();
    };

    auto req_task = requester();
    loop.schedule(peer.run());
    loop.schedule(req_task);
    EXPECT(loop.run() == 0);

    ASSERT(tp->outgoing().size() >= 3U);
    EXPECT(zest::contains(tp->outgoing()[0], R"("my-token")"));
}

ZEST_CASE(create_failure) {
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
    EXPECT(loop.run() == 0);

    EXPECT(!create_result.has_value());
    EXPECT(create_result.error().message == "not supported");
}

};  // namespace kota::ipc

}  // namespace
}  // namespace kota::ipc
