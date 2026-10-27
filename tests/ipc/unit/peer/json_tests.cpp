#include <string>

#include "ipc/harness/codec_json.h"
#include "ipc/harness/peer_fixture.h"
#include "ipc/harness/peer_suite/cancel.h"
#include "ipc/harness/peer_suite/dispatch.h"
#include "ipc/harness/peer_suite/ending.h"
#include "ipc/harness/peer_suite/incoming.h"
#include "ipc/harness/peer_suite/lifecycle.h"
#include "ipc/harness/peer_suite/limit.h"
#include "ipc/harness/peer_suite/link.h"
#include "ipc/harness/peer_suite/requests.h"
#include "ipc/harness/peer_suite/timeout.h"
#include "ipc/harness/peer_suite/unreadable.h"
#include "kota/ipc/peer/json.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::ipc {

namespace {

using Fixture = test::PeerFixture<test::JSONAdapter>;
using Kit = test::PeerKit<test::JSONAdapter>;
using protocol::ErrorCode;
using test::AddParams;
using test::AddResult;
using test::code_of;
using test::Message;

ZEST_SUITE(ipc_peer_json) {

ZEST_CASE_GROUP(dispatch) {
    test::peer_dispatch(Kit{add_case});
}

ZEST_CASE_GROUP(requests) {
    test::peer_requests(Kit{add_case});
}

ZEST_CASE_GROUP(cancel) {
    test::peer_cancel(Kit{add_case});
}

ZEST_CASE_GROUP(incoming) {
    test::peer_incoming(Kit{add_case});
}

ZEST_CASE_GROUP(timeout) {
    test::peer_timeout(Kit{add_case});
}

ZEST_CASE_GROUP(lifecycle) {
    test::peer_lifecycle(Kit{add_case});
}

ZEST_CASE_GROUP(ending) {
    test::peer_ending(Kit{add_case});
}

ZEST_CASE_GROUP(limit) {
    test::peer_limit(Kit{add_case});
}

ZEST_CASE_GROUP(link) {
    test::peer_link(Kit{add_case});
}

ZEST_CASE_GROUP(unreadable) {
    test::peer_unreadable(Kit{add_case});
}

ZEST_CASE(error_data_crosses_between_peers) {
    test::error_data_crosses_between_peers<test::JSONAdapter>();
}

ZEST_CASE(object_without_method_or_id_is_answered_with_invalid_request) {
    Fixture f;
    f.remote.send("{}");
    f.remote.end_input();

    auto [ran] = f.run(f.peer.run());
    ZEXPECT(ran.has_value());
    const auto& written = f.written();
    ZASSERT(written.size() == 1U);
    ZEXPECT(written[0].kind == Message::Kind::Error);
    ZEXPECT(code_of(written[0].error) == ErrorCode::InvalidRequest);
    ZEXPECT(written[0].error.message == "message must contain method or id");
    ZEXPECT(!written[0].id.has_value());
}

// LSP allows no null request id; JSON-RPC answers one as an invalid request.
ZEST_CASE(request_with_a_null_id_is_answered_with_invalid_request) {
    Fixture f;
    bool called = false;
    f.peer.on_request([&](Fixture::Context&, const AddParams&) -> RequestResult<AddParams> {
        called = true;
        co_return AddResult{};
    });
    f.remote.send(R"({"jsonrpc":"2.0","id":null,"method":"test/add","params":{"a":1,"b":2}})");
    f.remote.end_input();

    auto [ran] = f.run(f.peer.run());
    ZEXPECT(ran.has_value());
    ZEXPECT(!called);
    const auto& written = f.written();
    ZASSERT(written.size() == 1U);
    ZEXPECT(written[0].kind == Message::Kind::Error);
    ZEXPECT(code_of(written[0].error) == ErrorCode::InvalidRequest);
    ZEXPECT(!written[0].id.has_value());
}

// The remote ends the input only once the request has failed, so the end
// cannot be what fails it.
ZEST_CASE(error_response_with_a_malformed_error_fails_its_request) {
    Fixture f;
    event answered;
    auto ask = [&]() -> task<Error> {
        auto result = co_await f.peer.send_request(AddParams{});
        answered.set();
        co_return result.has_error() ? result.error() : Error("no error");
    };
    auto remote = [&]() -> task<> {
        co_await f.next();
        f.remote.send(R"({"jsonrpc":"2.0","id":1,"error":{"code":"E1","message":"x"}})");
        co_await answered.wait();
        f.remote.end_input();
    };

    auto [ran, failure, scripted] = f.run(f.peer.run(), ask(), remote());
    ZEXPECT(ran.has_value());
    ZASSERT(failure.has_value());
    ZEXPECT(code_of(*failure) == ErrorCode::InvalidRequest);
    ZEXPECT(f.written().size() == 1U);
}

ZEST_CASE(request_with_a_malformed_member_is_answered_with_its_id) {
    Fixture f;
    f.remote.send(R"({"jsonrpc":"2.0","id":5,"method":7})");
    f.remote.end_input();

    auto [ran] = f.run(f.peer.run());
    ZEXPECT(ran.has_value());
    const auto& written = f.written();
    ZASSERT(written.size() == 1U);
    ZEXPECT(written[0].kind == Message::Kind::Error);
    ZEXPECT(written[0].id == protocol::RequestID(5));
    ZEXPECT(code_of(written[0].error) == ErrorCode::InvalidRequest);
}

// JSON-RPC never answers a notification, even one it cannot read.
ZEST_CASE(deeply_nested_notification_is_dropped) {
    Fixture f;
    bool called = false;
    f.peer.on_notification([&](const test::NoteParams&) { called = true; });
    auto deep = std::string(50000, '[') + std::string(50000, ']');
    f.remote.send(R"({"jsonrpc":"2.0","method":"test/note","params":)" + deep + "}");
    f.remote.end_input();

    auto [ran] = f.run(f.peer.run());
    ZEXPECT(ran.has_value());
    ZEXPECT(!called);
    ZEXPECT(f.written().empty());
}

// Answered under the id it names, and then the end.
ZEST_CASE(unreadable_request_is_answered_before_it_ends_the_conversation_under_end) {
    Fixture f;
    f.peer.set_violation_policy(ViolationPolicy::End);
    f.remote.send(R"({"jsonrpc":"2.0","id":4,"method":5})");

    auto [ran] = f.run(f.peer.run());
    ZEXPECT(ran.has_value());
    const auto& written = f.written();
    ZASSERT(written.size() == 1U);
    ZEXPECT(written[0].id == protocol::RequestID(4));
    ZEXPECT(code_of(written[0].error) == ErrorCode::InvalidRequest);
    auto ended = f.peer.ended();
    ZASSERT(ended.has_value());
    ZEXPECT(ended->kind == ConversationEnd::Kind::Malformed);
    ZEXPECT(ended->message == written[0].error.message);
}

// It fails the request it names; the other would wait for good.
ZEST_CASE(unreadable_response_ends_the_conversation_under_end) {
    Fixture f;
    f.peer.set_violation_policy(ViolationPolicy::End);
    auto remote = [&]() -> task<> {
        co_await f.next();
        co_await f.next();
        f.remote.send(R"({"jsonrpc":"1.0","id":1,"result":0})");
    };

    auto [ran, first, second, scripted] = f.run(f.peer.run(),
                                                f.peer.send_request(AddParams{}),
                                                f.peer.send_request(AddParams{}),
                                                remote());
    ZEXPECT(ran.has_value());
    ZASSERT(first.has_error());
    ZEXPECT(code_of(first.error()) == ErrorCode::InvalidRequest);
    ZASSERT(second.has_error());
    ZEXPECT(code_of(second.error()) == ErrorCode::ConnectionClosed);
    auto ended = f.peer.ended();
    ZASSERT(ended.has_value());
    ZEXPECT(ended->kind == ConversationEnd::Kind::Malformed);
    ZEXPECT(ended->message == R"(malformed response: jsonrpc must be "2.0")");
}

// JSON-RPC never answers it, and it ends the conversation all the same.
ZEST_CASE(unreadable_notification_ends_the_conversation_under_end) {
    Fixture f;
    f.peer.set_violation_policy(ViolationPolicy::End);
    f.remote.send(R"({"jsonrpc":"1.0","method":"test/note","params":{"text":"x"}})");

    auto [ran] = f.run(f.peer.run());
    ZEXPECT(ran.has_value());
    ZEXPECT(f.written().empty());
    auto ended = f.peer.ended();
    ZASSERT(ended.has_value());
    ZEXPECT(ended->kind == ConversationEnd::Kind::Malformed);
    ZEXPECT(ended->message == R"(jsonrpc must be "2.0")");
}

};  // ZEST_SUITE(ipc_peer_json)

}  // namespace

}  // namespace kota::ipc
