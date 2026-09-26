#include <cstdint>
#include <string>
#include <variant>

#include "kota/ipc/codec/bincode.h"
#include "kota/ipc/codec/json.h"
#include "kota/zest/zest.h"

namespace kota::ipc {

// ============================================================================
// Helpers
// ============================================================================

namespace {

template <typename T>
bool holds(const IncomingMessage& msg) {
    return std::holds_alternative<T>(msg);
}

template <typename T>
const T& get(const IncomingMessage& msg) {
    return std::get<T>(msg);
}

// ============================================================================
// Group 1: JsonCodec — parse_message boundary tests
// ============================================================================

ZEST_SUITE(ipc_json_codec_parse){

    // 1.1 Valid request (method + integer id + params)
    ZEST_CASE(valid_request){JsonCodec codec;
auto msg =
    codec.parse_message(R"({"jsonrpc":"2.0","id":1,"method":"test/add","params":{"a":1,"b":2}})");

ASSERT(holds<IncomingRequest>(msg));
auto& req = get<IncomingRequest>(msg);
EXPECT(req.id == protocol::RequestID{std::int64_t(1)});
EXPECT(req.method == "test/add");
EXPECT(!req.params.empty());

}  // namespace

// 1.2 Valid notification (method, no id)
ZEST_CASE(valid_notification) {
    JsonCodec codec;
    auto msg =
        codec.parse_message(R"({"jsonrpc":"2.0","method":"$/progress","params":{"token":1}})");

    ASSERT(holds<IncomingNotification>(msg));
    auto& note = get<IncomingNotification>(msg);
    EXPECT(note.method == "$/progress");
    EXPECT(!note.params.empty());
}

// 1.3 Valid success response (id + result, no method)
ZEST_CASE(valid_success_response) {
    JsonCodec codec;
    auto msg = codec.parse_message(R"({"jsonrpc":"2.0","id":42,"result":{"sum":3}})");

    ASSERT(holds<IncomingResponse>(msg));
    auto& resp = get<IncomingResponse>(msg);
    EXPECT(resp.id == protocol::RequestID{std::int64_t(42)});
    EXPECT(!resp.result.empty());
}

// 1.4 Valid error response (id + error, no method) — verify code/message/data
ZEST_CASE(valid_error_response) {
    JsonCodec codec;
    auto msg = codec.parse_message(
        R"({"jsonrpc":"2.0","id":7,"error":{"code":-32601,"message":"method not found","data":"extra"}})");

    ASSERT(holds<IncomingErrorResponse>(msg));
    auto& err = get<IncomingErrorResponse>(msg);
    EXPECT(err.id == protocol::RequestID{std::int64_t(7)});
    EXPECT(err.error.code == static_cast<protocol::integer>(protocol::ErrorCode::MethodNotFound));
    EXPECT(err.error.message == "method not found");
    ASSERT(err.error.data.has_value());
}

// 1.5 JSON parse failure (invalid JSON)
ZEST_CASE(invalid_json) {
    JsonCodec codec;
    auto msg = codec.parse_message("{not valid json");

    ASSERT(holds<IncomingParseError>(msg));
    auto& err = get<IncomingParseError>(msg);
    EXPECT(err.error.code == static_cast<protocol::integer>(protocol::ErrorCode::ParseError));
}

// 1.6 Method present but id is null → treated as notification (null id = absent)
ZEST_CASE(null_id_method) {
    JsonCodec codec;
    auto msg =
        codec.parse_message(R"({"jsonrpc":"2.0","id":null,"method":"test/foo","params":{}})");

    ASSERT(holds<IncomingNotification>(msg));
    auto& note = get<IncomingNotification>(msg);
    EXPECT(note.method == "test/foo");
}

// 1.7 Empty object — no method, no id
ZEST_CASE(empty_object) {
    JsonCodec codec;
    auto msg = codec.parse_message(R"({})");

    ASSERT(holds<IncomingParseError>(msg));
    auto& err = get<IncomingParseError>(msg);
    EXPECT(err.error.code == static_cast<protocol::integer>(protocol::ErrorCode::InvalidRequest));
}

// 1.8 Response with both result and error → error response (validation error)
ZEST_CASE(both_result_error) {
    JsonCodec codec;
    auto msg = codec.parse_message(
        R"({"jsonrpc":"2.0","id":5,"result":{"x":1},"error":{"code":-1,"message":"oops"}})");

    ASSERT(holds<IncomingErrorResponse>(msg));
    auto& err = get<IncomingErrorResponse>(msg);
    EXPECT(err.id == protocol::RequestID{std::int64_t(5)});
    EXPECT(err.error.code == static_cast<protocol::integer>(protocol::ErrorCode::InvalidRequest));
}

// 1.9 Response with neither result nor error → error response (validation error)
ZEST_CASE(neither_result_error) {
    JsonCodec codec;
    auto msg = codec.parse_message(R"({"jsonrpc":"2.0","id":5})");

    ASSERT(holds<IncomingErrorResponse>(msg));
    auto& err = get<IncomingErrorResponse>(msg);
    EXPECT(err.id == protocol::RequestID{std::int64_t(5)});
    EXPECT(err.error.code == static_cast<protocol::integer>(protocol::ErrorCode::InvalidRequest));
}

// 1.10 Error response with nested data object
ZEST_CASE(nested_error_data) {
    JsonCodec codec;
    auto msg = codec.parse_message(
        R"({"jsonrpc":"2.0","id":9,"error":{"code":-32000,"message":"fail","data":{"retry":true,"count":3}}})");

    ASSERT(holds<IncomingErrorResponse>(msg));
    auto& err = get<IncomingErrorResponse>(msg);
    EXPECT(err.error.code == static_cast<protocol::integer>(protocol::ErrorCode::RequestFailed));
    EXPECT(err.error.message == "fail");
    ASSERT(err.error.data.has_value());
}

// 1.11 Request with missing params → params is empty string
ZEST_CASE(request_missing_params) {
    JsonCodec codec;
    auto msg = codec.parse_message(R"({"jsonrpc":"2.0","id":1,"method":"test/noparams"})");

    ASSERT(holds<IncomingRequest>(msg));
    auto& req = get<IncomingRequest>(msg);
    EXPECT(req.method == "test/noparams");
    EXPECT(req.params.empty());
}

// 1.12 Request with string id → parsed as request with string RequestID
ZEST_CASE(string_id_accepted) {
    JsonCodec codec;
    auto msg =
        codec.parse_message(R"({"jsonrpc":"2.0","id":"abc","method":"test/foo","params":{}})");

    ASSERT(holds<IncomingRequest>(msg));
    auto& req = get<IncomingRequest>(msg);
    EXPECT(std::holds_alternative<std::string>(req.id));
    EXPECT(std::get<std::string>(req.id) == "abc");
    EXPECT(req.method == "test/foo");
}

};  // namespace kota::ipc

// ============================================================================
// Group 1: BincodeCodec — parse_message boundary tests
// ============================================================================

ZEST_SUITE(ipc_bincode_codec_parse){

    // Helper: encode then parse to test the parse side via known-good encoding
    // (Bincode has no hand-written payloads like JSON, so we roundtrip through encode)

    // 1.1 Valid request
    ZEST_CASE(valid_request){BincodeCodec codec;
auto encoded = codec.encode_request(protocol::RequestID{std::int64_t(1)}, "test/add", R"({"a":1})");
ASSERT(encoded.has_value());

auto msg = codec.parse_message(*encoded);
ASSERT(holds<IncomingRequest>(msg));
auto& req = get<IncomingRequest>(msg);
EXPECT(req.id == protocol::RequestID{std::int64_t(1)});
EXPECT(req.method == "test/add");
}

// 1.2 Valid notification
ZEST_CASE(valid_notification) {
    BincodeCodec codec;
    auto encoded = codec.encode_notification("$/progress", R"({"token":1})");
    ASSERT(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT(holds<IncomingNotification>(msg));
    auto& note = get<IncomingNotification>(msg);
    EXPECT(note.method == "$/progress");
}

// 1.3 Valid success response
ZEST_CASE(valid_success_response) {
    BincodeCodec codec;
    auto encoded =
        codec.encode_success_response(protocol::RequestID{std::int64_t(42)}, R"({"sum":3})");
    ASSERT(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT(holds<IncomingResponse>(msg));
    auto& resp = get<IncomingResponse>(msg);
    EXPECT(resp.id == protocol::RequestID{std::int64_t(42)});
}

// 1.4 Valid error response
ZEST_CASE(valid_error_response) {
    BincodeCodec codec;
    Error error(protocol::ErrorCode::MethodNotFound, "method not found");
    auto encoded = codec.encode_error_response(protocol::RequestID{std::int64_t(7)}, error);
    ASSERT(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT(holds<IncomingErrorResponse>(msg));
    auto& err = get<IncomingErrorResponse>(msg);
    EXPECT(err.id == protocol::RequestID{std::int64_t(7)});
    EXPECT(err.error.code == static_cast<protocol::integer>(protocol::ErrorCode::MethodNotFound));
    EXPECT(err.error.message == "method not found");
}

// 1.5 Invalid binary data → parse error
ZEST_CASE(invalid_binary) {
    BincodeCodec codec;
    auto msg = codec.parse_message("not valid bincode\xff\xfe");

    ASSERT(holds<IncomingParseError>(msg));
    auto& err = get<IncomingParseError>(msg);
    EXPECT(err.error.code == static_cast<protocol::integer>(protocol::ErrorCode::ParseError));
}

// 1.5b Empty payload → parse error
ZEST_CASE(empty_payload) {
    BincodeCodec codec;
    auto msg = codec.parse_message("");

    ASSERT(holds<IncomingParseError>(msg));
}

// 1.11 Request with empty params
ZEST_CASE(request_empty_params) {
    BincodeCodec codec;
    auto encoded = codec.encode_request(protocol::RequestID{std::int64_t(1)}, "test/noparams", "");
    ASSERT(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT(holds<IncomingRequest>(msg));
    auto& req = get<IncomingRequest>(msg);
    EXPECT(req.params.empty());
}
}
;  // ZEST_SUITE(ipc_bincode_codec_parse)

// ============================================================================
// Group 2: Codec — encode/parse roundtrip consistency
// ============================================================================

ZEST_SUITE(ipc_json_codec_roundtrip){

    // 2.1 encode_request → parse_message roundtrip
    ZEST_CASE(request_roundtrip){JsonCodec codec;
auto encoded =
    codec.encode_request(protocol::RequestID{std::int64_t(99)}, "math/add", R"({"a":1,"b":2})");
ASSERT(encoded.has_value());

auto msg = codec.parse_message(*encoded);
ASSERT(holds<IncomingRequest>(msg));
auto& req = get<IncomingRequest>(msg);
EXPECT(req.id == protocol::RequestID{std::int64_t(99)});
EXPECT(req.method == "math/add");
EXPECT(!req.params.empty());
}

// 2.2 encode_notification → parse_message roundtrip
ZEST_CASE(notification_roundtrip) {
    JsonCodec codec;
    auto encoded = codec.encode_notification("log/info", R"({"text":"hello"})");
    ASSERT(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT(holds<IncomingNotification>(msg));
    auto& note = get<IncomingNotification>(msg);
    EXPECT(note.method == "log/info");
}

// 2.3 encode_success_response → parse_message roundtrip
ZEST_CASE(success_response_roundtrip) {
    JsonCodec codec;
    auto encoded =
        codec.encode_success_response(protocol::RequestID{std::int64_t(10)}, R"({"value":42})");
    ASSERT(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT(holds<IncomingResponse>(msg));
    auto& resp = get<IncomingResponse>(msg);
    EXPECT(resp.id == protocol::RequestID{std::int64_t(10)});
}

// 2.4 encode_error_response → parse_message roundtrip — Error fields preserved
ZEST_CASE(error_response_roundtrip) {
    JsonCodec codec;
    Error original(protocol::ErrorCode::InternalError, "something broke");
    auto encoded = codec.encode_error_response(protocol::RequestID{std::int64_t(20)}, original);
    ASSERT(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT(holds<IncomingErrorResponse>(msg));
    auto& err = get<IncomingErrorResponse>(msg);
    EXPECT(err.id == protocol::RequestID{std::int64_t(20)});
    EXPECT(err.error.code == original.code);
    EXPECT(err.error.message == original.message);
}

// 2.5 Empty params roundtrip
ZEST_CASE(empty_params_roundtrip) {
    JsonCodec codec;
    auto encoded = codec.encode_request(protocol::RequestID{std::int64_t(1)}, "test/empty", "");
    ASSERT(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT(holds<IncomingRequest>(msg));
}
}
;  // ZEST_SUITE(ipc_json_codec_roundtrip)

ZEST_SUITE(ipc_bincode_codec_roundtrip){

    // 2.1 request roundtrip
    ZEST_CASE(request_roundtrip){BincodeCodec codec;
auto encoded =
    codec.encode_request(protocol::RequestID{std::int64_t(99)}, "math/add", R"({"a":1})");
ASSERT(encoded.has_value());

auto msg = codec.parse_message(*encoded);
ASSERT(holds<IncomingRequest>(msg));
auto& req = get<IncomingRequest>(msg);
EXPECT(req.id == protocol::RequestID{std::int64_t(99)});
EXPECT(req.method == "math/add");
}

// 2.2 notification roundtrip
ZEST_CASE(notification_roundtrip) {
    BincodeCodec codec;
    auto encoded = codec.encode_notification("log/info", R"({"text":"hello"})");
    ASSERT(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT(holds<IncomingNotification>(msg));
    auto& note = get<IncomingNotification>(msg);
    EXPECT(note.method == "log/info");
}

// 2.3 success response roundtrip
ZEST_CASE(success_response_roundtrip) {
    BincodeCodec codec;
    auto encoded =
        codec.encode_success_response(protocol::RequestID{std::int64_t(10)}, R"({"value":42})");
    ASSERT(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT(holds<IncomingResponse>(msg));
    auto& resp = get<IncomingResponse>(msg);
    EXPECT(resp.id == protocol::RequestID{std::int64_t(10)});
}

// 2.4 error response roundtrip
ZEST_CASE(error_response_roundtrip) {
    BincodeCodec codec;
    Error original(protocol::ErrorCode::InternalError, "something broke");
    auto encoded = codec.encode_error_response(protocol::RequestID{std::int64_t(20)}, original);
    ASSERT(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT(holds<IncomingErrorResponse>(msg));
    auto& err = get<IncomingErrorResponse>(msg);
    EXPECT(err.id == protocol::RequestID{std::int64_t(20)});
    EXPECT(err.error.code == original.code);
    EXPECT(err.error.message == original.message);
}

// 2.5 empty params roundtrip
ZEST_CASE(empty_params_roundtrip) {
    BincodeCodec codec;
    auto encoded = codec.encode_request(protocol::RequestID{std::int64_t(1)}, "test/empty", "");
    ASSERT(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT(holds<IncomingRequest>(msg));
    auto& req = get<IncomingRequest>(msg);
    EXPECT(req.params.empty());
}
}
;  // ZEST_SUITE(ipc_bincode_codec_roundtrip)

}  // namespace
}  // namespace kota::ipc
