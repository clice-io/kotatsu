#include "ipc/harness/codec_json.h"
#include "ipc/harness/peer_fixture.h"
#include "ipc/harness/peer_suite/cancel.h"
#include "ipc/harness/peer_suite/dispatch.h"
#include "ipc/harness/peer_suite/lifecycle.h"
#include "ipc/harness/peer_suite/link.h"
#include "ipc/harness/peer_suite/requests.h"
#include "ipc/harness/peer_suite/timeout.h"
#include "kota/ipc/codec/json.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::ipc {

namespace {

using Fixture = test::PeerFixture<test::JsonAdapter>;
using Kit = test::PeerKit<test::JsonAdapter>;
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

ZEST_CASE_GROUP(timeout) {
    test::peer_timeout(Kit{add_case});
}

ZEST_CASE_GROUP(lifecycle) {
    test::peer_lifecycle(Kit{add_case});
}

ZEST_CASE_GROUP(link) {
    test::peer_link(Kit{add_case});
}

ZEST_CASE(raw_value_result_is_sent_as_it_is) {
    test::raw_value_result_is_sent_as_it_is<test::JsonAdapter>();
}

ZEST_CASE(request_without_params_is_answered_with_invalid_params) {
    test::request_without_params_is_answered_with_invalid_params<test::JsonAdapter>();
}

ZEST_CASE(error_data_crosses_between_peers) {
    test::error_data_crosses_between_peers<test::JsonAdapter>();
}

// N1: a request sent after the input ended waits for an answer for good.
ZEST_CASE(request_from_a_handler_after_end_of_input_fails, skip = true) {
    test::request_from_a_handler_after_end_of_input_fails<test::JsonAdapter>();
}

// N4: close_output() closes at once, so the queued message fails to write.
ZEST_CASE(close_output_writes_queued_messages_first, skip = true) {
    test::close_output_writes_queued_messages_first<test::JsonAdapter>();
}

// N4: a send after close_output() is queued, fails to write, and the failure
// closes the input too.
ZEST_CASE(send_after_close_output_fails, skip = true) {
    test::send_after_close_output_fails<test::JsonAdapter>();
}

ZEST_CASE(object_without_method_or_id_is_answered_with_invalid_request) {
    Fixture f;
    f.remote.send("{}");
    f.remote.end_input();

    auto [ran] = f.run(f.peer.run());
    EXPECT(ran.has_value());
    const auto& written = f.written();
    ASSERT(written.size() == 1U);
    EXPECT(written[0].kind == Message::Kind::Error);
    EXPECT(code_of(written[0].error) == ErrorCode::InvalidRequest);
    EXPECT(written[0].error.message == "message must contain method or id");
    EXPECT(!written[0].id.has_value());
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
    EXPECT(ran.has_value());
    EXPECT(!called);
    const auto& written = f.written();
    ASSERT(written.size() == 1U);
    EXPECT(written[0].kind == Message::Kind::Error);
    EXPECT(code_of(written[0].error) == ErrorCode::InvalidRequest);
    EXPECT(!written[0].id.has_value());
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
    EXPECT(ran.has_value());
    ASSERT(failure.has_value());
    EXPECT(code_of(*failure) == ErrorCode::InvalidRequest);
    EXPECT(f.written().size() == 1U);
}

ZEST_CASE(request_with_a_malformed_member_is_answered_with_its_id) {
    Fixture f;
    f.remote.send(R"({"jsonrpc":"2.0","id":5,"method":7})");
    f.remote.end_input();

    auto [ran] = f.run(f.peer.run());
    EXPECT(ran.has_value());
    const auto& written = f.written();
    ASSERT(written.size() == 1U);
    EXPECT(written[0].kind == Message::Kind::Error);
    EXPECT(written[0].id == protocol::RequestID(5));
    EXPECT(code_of(written[0].error) == ErrorCode::InvalidRequest);
}

};  // ZEST_SUITE(ipc_peer_json)

}  // namespace

}  // namespace kota::ipc
