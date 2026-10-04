#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ipc/harness/codec_bincode.h"
#include "ipc/harness/codec_json.h"
#include "ipc/harness/peer_fixture.h"
#include "kota/ipc/codec/bincode.h"
#include "kota/ipc/codec/json.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"
#include "kota/ipc/lsp/progress.h"

namespace kota::ipc::lsp {

namespace {

using Fixture = test::PeerFixture<test::JSONAdapter>;

ZEST_SUITE(ipc_lsp_progress, Fixture) {

/// Answers the create request with `reply`, then receives `count` more
/// messages and ends the input; returns every message it received.
task<std::vector<std::string>> answer_create(std::string reply, std::size_t count) {
    std::vector<std::string> received;
    while(auto message = co_await remote.receive()) {
        received.push_back(std::move(*message));
        if(received.size() == 1) {
            remote.send(reply);
        }
        if(received.size() == count + 1) {
            break;
        }
    }
    remote.end_input();
    co_return received;
}

ZEST_CASE(create_registers_the_token) {
    ProgressReporter reporter(peer, protocol::ProgressToken(1));

    auto [ran, created, received] =
        run(peer.run(),
            reporter.create(),
            answer_create(R"({"jsonrpc":"2.0","id":1,"result":null})", 0));
    ZEXPECT(ran.has_value());
    ZEXPECT(created.has_value());
    ZASSERT(received.has_value());
    ZEXPECT(
        *received ==
        std::vector<std::string>{
            R"({"jsonrpc":"2.0","id":1,"method":"window/workDoneProgress/create","params":{"token":1}})",
        });
}

ZEST_CASE(create_failure_is_the_remote_error) {
    ProgressReporter reporter(peer, protocol::ProgressToken(1));

    auto [ran, created, received] =
        run(peer.run(),
            reporter.create(),
            answer_create(
                R"({"jsonrpc":"2.0","id":1,"error":{"code":-32600,"message":"not supported"}})",
                0));
    ZEXPECT(ran.has_value());
    ZASSERT(created.has_error());
    ZEXPECT(created.error().message == "not supported");
}

// begin leaves out a false `cancellable`; report keeps an explicit one,
// which there means "disable the cancel button" rather than "unchanged".
ZEST_CASE(begin_report_end_send_progress) {
    ProgressReporter reporter(peer, protocol::ProgressToken(42));
    std::vector<bool> sent;
    auto report = [&]() -> task<void, Error> {
        co_await reporter.create().or_fail();
        sent.push_back(
            reporter.begin("Indexing", "Starting...", protocol::uinteger(0)).has_value());
        sent.push_back(reporter.report("50% done", protocol::uinteger(50), false).has_value());
        sent.push_back(reporter.end("Complete").has_value());
    };

    auto [ran, reported, received] =
        run(peer.run(), report(), answer_create(R"({"jsonrpc":"2.0","id":1,"result":null})", 3));
    ZEXPECT(ran.has_value());
    ZEXPECT(reported.has_value());
    ZEXPECT(sent == std::vector{true, true, true});
    ZASSERT(received.has_value());
    ZASSERT(received->size() == 4U);
    ZEXPECT(
        (*received)[1] ==
        R"({"jsonrpc":"2.0","method":"$/progress","params":{"token":42,"value":{"kind":"begin","title":"Indexing","message":"Starting...","percentage":0}}})");
    ZEXPECT(
        (*received)[2] ==
        R"({"jsonrpc":"2.0","method":"$/progress","params":{"token":42,"value":{"kind":"report","cancellable":false,"message":"50% done","percentage":50}}})");
    ZEXPECT(
        (*received)[3] ==
        R"({"jsonrpc":"2.0","method":"$/progress","params":{"token":42,"value":{"kind":"end","message":"Complete"}}})");
}

ZEST_CASE(string_token_is_sent_as_a_string) {
    ProgressReporter reporter(peer, protocol::ProgressToken(std::string("my-token")));
    auto report = [&]() -> task<void, Error> {
        co_await reporter.create().or_fail();
        co_await or_fail(reporter.begin("Building"));
    };

    auto [ran, reported, received] =
        run(peer.run(), report(), answer_create(R"({"jsonrpc":"2.0","id":1,"result":null})", 1));
    ZEXPECT(ran.has_value());
    ZEXPECT(reported.has_value());
    ZASSERT(received.has_value());
    ZASSERT(received->size() == 2U);
    ZEXPECT(zest::contains((*received)[0], R"("token":"my-token")"));
    ZEXPECT(zest::contains((*received)[1], R"("token":"my-token")"));
}

};  // ZEST_SUITE(ipc_lsp_progress)

// A binary codec writes the notification as the ProgressParams it is, so the
// remote reads it as one.
ZEST_SUITE(ipc_lsp_progress_bincode, test::PeerFixture<test::BincodeAdapter>) {

ZEST_CASE(progress_reads_as_progress_params) {
    ProgressReporter reporter(peer, protocol::ProgressToken(7));
    auto report = [&]() -> task<> {
        ZEXPECT(reporter.begin("Indexing", {}, protocol::uinteger(10)).has_value());
        remote.end_input();
        co_return;
    };

    auto [ran, reported] = run(peer.run(), report());
    ZEXPECT(ran.has_value());
    const auto& written = this->written();
    ZASSERT(written.size() == 1U);
    ZEXPECT(written[0].method == "$/progress");
    auto params = test::BincodeAdapter::decode<protocol::ProgressParams>(written[0].body);
    ZASSERT(params.has_value());
    ZEXPECT(params->token == protocol::ProgressToken(7));
    auto begin = codec::dyn::from_dyn<protocol::WorkDoneProgressBegin, lsp_config>(params->value);
    ZASSERT(begin.has_value());
    ZEXPECT(begin->title == "Indexing");
    ZEXPECT(begin->percentage == std::optional<protocol::uinteger>(10U));
}

};  // ZEST_SUITE(ipc_lsp_progress_bincode)

}  // namespace

}  // namespace kota::ipc::lsp
