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

using Fixture = test::PeerFixture<test::JsonWire>;
using Kit = test::PeerKit<test::JsonWire>;
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
    test::raw_value_result_is_sent_as_it_is<test::JsonWire>();
}

ZEST_CASE(request_without_params_is_answered_with_invalid_params) {
    test::request_without_params_is_answered_with_invalid_params<test::JsonWire>();
}

ZEST_CASE(error_data_crosses_between_peers) {
    test::error_data_crosses_between_peers<test::JsonWire>();
}

// P1.2: the reply to an unparsable message carries id 0 instead of null.
ZEST_CASE(unparsable_message_is_answered_without_an_id, skip = true) {
    test::unparsable_message_is_answered_without_an_id<test::JsonWire>();
}

// P1.2: an error response with a null id is taken for an invalid request and
// answered.
ZEST_CASE(error_response_without_an_id_is_not_answered, skip = true) {
    test::error_response_without_an_id_is_not_answered<test::JsonWire>();
}

// N1: a request sent after the input ended waits for an answer for good.
ZEST_CASE(request_from_a_handler_after_end_of_input_fails, skip = true) {
    test::request_from_a_handler_after_end_of_input_fails<test::JsonWire>();
}

// N4: close_output() closes at once, so the queued message fails to write.
ZEST_CASE(close_output_writes_queued_messages_first, skip = true) {
    test::close_output_writes_queued_messages_first<test::JsonWire>();
}

// N4: a send after close_output() is queued, fails to write, and the failure
// closes the input too.
ZEST_CASE(send_after_close_output_fails, skip = true) {
    test::send_after_close_output_fails<test::JsonWire>();
}

// The id of an invalid request is answered by the P1.2 cases above.
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
}

// P1.2: a request with a null id is dispatched as a notification.
ZEST_CASE(request_with_a_null_id_is_answered_with_invalid_request, skip = true) {
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

// N2: an error whose code is not an integer makes the whole response a parse
// error, which is answered, while request 1 waits for good. The remote ends
// the input only once the request has failed, so the end cannot fail it.
ZEST_CASE(error_response_with_a_malformed_error_fails_its_request, skip = true) {
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

// N2: a request whose method is not a string is answered as unparsable, with
// id 0 instead of its own.
ZEST_CASE(request_with_a_malformed_member_is_answered_with_its_id, skip = true) {
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
