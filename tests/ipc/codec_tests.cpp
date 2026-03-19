#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include "eventide/ipc/bincode_codec.h"
#include "eventide/ipc/json_codec.h"
#include "eventide/zest/zest.h"

namespace eventide::ipc {

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

}  // namespace

// ============================================================================
// Group 1: JsonCodec — parse_message boundary tests
// ============================================================================

TEST_SUITE(ipc_json_codec_parse) {

// 1.1 Valid request (method + integer id + params)
TEST_CASE(valid_request) {
    JsonCodec codec;
    auto msg = codec.parse_message(
        R"({"jsonrpc":"2.0","id":1,"method":"test/add","params":{"a":1,"b":2}})");

    ASSERT_TRUE(holds<IncomingRequest>(msg));
    auto& req = get<IncomingRequest>(msg);
    EXPECT_EQ(req.id, protocol::RequestID{std::int64_t(1)});
    EXPECT_EQ(req.method, "test/add");
    EXPECT_FALSE(req.params.empty());
}

// 1.2 Valid notification (method, no id)
TEST_CASE(valid_notification) {
    JsonCodec codec;
    auto msg =
        codec.parse_message(R"({"jsonrpc":"2.0","method":"$/progress","params":{"token":1}})");

    ASSERT_TRUE(holds<IncomingNotification>(msg));
    auto& note = get<IncomingNotification>(msg);
    EXPECT_EQ(note.method, "$/progress");
    EXPECT_FALSE(note.params.empty());
}

// 1.3 Valid success response (id + result, no method)
TEST_CASE(valid_success_response) {
    JsonCodec codec;
    auto msg = codec.parse_message(R"({"jsonrpc":"2.0","id":42,"result":{"sum":3}})");

    ASSERT_TRUE(holds<IncomingResponse>(msg));
    auto& resp = get<IncomingResponse>(msg);
    EXPECT_EQ(resp.id, protocol::RequestID{std::int64_t(42)});
    EXPECT_FALSE(resp.result.empty());
}

// 1.4 Valid error response (id + error, no method) — verify code/message/data
TEST_CASE(valid_error_response) {
    JsonCodec codec;
    auto msg = codec.parse_message(
        R"({"jsonrpc":"2.0","id":7,"error":{"code":-32601,"message":"method not found","data":"extra"}})");

    ASSERT_TRUE(holds<IncomingErrorResponse>(msg));
    auto& err = get<IncomingErrorResponse>(msg);
    EXPECT_EQ(err.id, protocol::RequestID{std::int64_t(7)});
    EXPECT_EQ(err.error.code, static_cast<protocol::integer>(protocol::ErrorCode::MethodNotFound));
    EXPECT_EQ(err.error.message, "method not found");
    ASSERT_TRUE(err.error.data.has_value());
}

// 1.5 JSON parse failure (invalid JSON)
TEST_CASE(invalid_json) {
    JsonCodec codec;
    auto msg = codec.parse_message("{not valid json");

    ASSERT_TRUE(holds<IncomingParseError>(msg));
    auto& err = get<IncomingParseError>(msg);
    EXPECT_EQ(err.error.code, static_cast<protocol::integer>(protocol::ErrorCode::ParseError));
}

// 1.6 Method present but id is null → treated as notification (null id = absent)
TEST_CASE(null_id_method) {
    JsonCodec codec;
    auto msg =
        codec.parse_message(R"({"jsonrpc":"2.0","id":null,"method":"test/foo","params":{}})");

    ASSERT_TRUE(holds<IncomingNotification>(msg));
    auto& note = get<IncomingNotification>(msg);
    EXPECT_EQ(note.method, "test/foo");
}

// 1.7 Empty object — no method, no id
TEST_CASE(empty_object) {
    JsonCodec codec;
    auto msg = codec.parse_message(R"({})");

    ASSERT_TRUE(holds<IncomingParseError>(msg));
    auto& err = get<IncomingParseError>(msg);
    EXPECT_EQ(err.error.code, static_cast<protocol::integer>(protocol::ErrorCode::InvalidRequest));
}

// 1.8 Response with both result and error → error response (validation error)
TEST_CASE(both_result_error) {
    JsonCodec codec;
    auto msg = codec.parse_message(
        R"({"jsonrpc":"2.0","id":5,"result":{"x":1},"error":{"code":-1,"message":"oops"}})");

    ASSERT_TRUE(holds<IncomingErrorResponse>(msg));
    auto& err = get<IncomingErrorResponse>(msg);
    EXPECT_EQ(err.id, protocol::RequestID{std::int64_t(5)});
    EXPECT_EQ(err.error.code, static_cast<protocol::integer>(protocol::ErrorCode::InvalidRequest));
}

// 1.9 Response with neither result nor error → error response (validation error)
TEST_CASE(neither_result_error) {
    JsonCodec codec;
    auto msg = codec.parse_message(R"({"jsonrpc":"2.0","id":5})");

    ASSERT_TRUE(holds<IncomingErrorResponse>(msg));
    auto& err = get<IncomingErrorResponse>(msg);
    EXPECT_EQ(err.id, protocol::RequestID{std::int64_t(5)});
    EXPECT_EQ(err.error.code, static_cast<protocol::integer>(protocol::ErrorCode::InvalidRequest));
}

// 1.10 Error response with nested data object
TEST_CASE(nested_error_data) {
    JsonCodec codec;
    auto msg = codec.parse_message(
        R"({"jsonrpc":"2.0","id":9,"error":{"code":-32000,"message":"fail","data":{"retry":true,"count":3}}})");

    ASSERT_TRUE(holds<IncomingErrorResponse>(msg));
    auto& err = get<IncomingErrorResponse>(msg);
    EXPECT_EQ(err.error.code, static_cast<protocol::integer>(protocol::ErrorCode::RequestFailed));
    EXPECT_EQ(err.error.message, "fail");
    ASSERT_TRUE(err.error.data.has_value());
}

// 1.11 Request with missing params → params is empty string
TEST_CASE(request_missing_params) {
    JsonCodec codec;
    auto msg = codec.parse_message(R"({"jsonrpc":"2.0","id":1,"method":"test/noparams"})");

    ASSERT_TRUE(holds<IncomingRequest>(msg));
    auto& req = get<IncomingRequest>(msg);
    EXPECT_EQ(req.method, "test/noparams");
    EXPECT_TRUE(req.params.empty());
}

// 1.12 Request with string id → parsed as request with string RequestID
TEST_CASE(string_id_accepted) {
    JsonCodec codec;
    auto msg =
        codec.parse_message(R"({"jsonrpc":"2.0","id":"abc","method":"test/foo","params":{}})");

    ASSERT_TRUE(holds<IncomingRequest>(msg));
    auto& req = get<IncomingRequest>(msg);
    EXPECT_TRUE(std::holds_alternative<std::string>(req.id));
    EXPECT_EQ(std::get<std::string>(req.id), "abc");
    EXPECT_EQ(req.method, "test/foo");
}

};  // TEST_SUITE(ipc_json_codec_parse)

// ============================================================================
// Group 1: BincodeCodec — parse_message boundary tests
// ============================================================================

TEST_SUITE(ipc_bincode_codec_parse) {

// Helper: encode then parse to test the parse side via known-good encoding
// (Bincode has no hand-written payloads like JSON, so we roundtrip through encode)

// 1.1 Valid request
TEST_CASE(valid_request) {
    BincodeCodec codec;
    auto encoded =
        codec.encode_request(protocol::RequestID{std::int64_t(1)}, "test/add", R"({"a":1})");
    ASSERT_TRUE(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT_TRUE(holds<IncomingRequest>(msg));
    auto& req = get<IncomingRequest>(msg);
    EXPECT_EQ(req.id, protocol::RequestID{std::int64_t(1)});
    EXPECT_EQ(req.method, "test/add");
}

// 1.2 Valid notification
TEST_CASE(valid_notification) {
    BincodeCodec codec;
    auto encoded = codec.encode_notification("$/progress", R"({"token":1})");
    ASSERT_TRUE(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT_TRUE(holds<IncomingNotification>(msg));
    auto& note = get<IncomingNotification>(msg);
    EXPECT_EQ(note.method, "$/progress");
}

// 1.3 Valid success response
TEST_CASE(valid_success_response) {
    BincodeCodec codec;
    auto encoded =
        codec.encode_success_response(protocol::RequestID{std::int64_t(42)}, R"({"sum":3})");
    ASSERT_TRUE(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT_TRUE(holds<IncomingResponse>(msg));
    auto& resp = get<IncomingResponse>(msg);
    EXPECT_EQ(resp.id, protocol::RequestID{std::int64_t(42)});
}

// 1.4 Valid error response
TEST_CASE(valid_error_response) {
    BincodeCodec codec;
    Error error(protocol::ErrorCode::MethodNotFound, "method not found");
    auto encoded = codec.encode_error_response(protocol::RequestID{std::int64_t(7)}, error);
    ASSERT_TRUE(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT_TRUE(holds<IncomingErrorResponse>(msg));
    auto& err = get<IncomingErrorResponse>(msg);
    EXPECT_EQ(err.id, protocol::RequestID{std::int64_t(7)});
    EXPECT_EQ(err.error.code, static_cast<protocol::integer>(protocol::ErrorCode::MethodNotFound));
    EXPECT_EQ(err.error.message, "method not found");
}

// 1.5 Invalid binary data → parse error
TEST_CASE(invalid_binary) {
    BincodeCodec codec;
    auto msg = codec.parse_message("not valid bincode\xff\xfe");

    ASSERT_TRUE(holds<IncomingParseError>(msg));
    auto& err = get<IncomingParseError>(msg);
    EXPECT_EQ(err.error.code, static_cast<protocol::integer>(protocol::ErrorCode::ParseError));
}

// 1.5b Empty payload → parse error
TEST_CASE(empty_payload) {
    BincodeCodec codec;
    auto msg = codec.parse_message("");

    ASSERT_TRUE(holds<IncomingParseError>(msg));
}

// 1.11 Request with empty params
TEST_CASE(request_empty_params) {
    BincodeCodec codec;
    auto encoded = codec.encode_request(protocol::RequestID{std::int64_t(1)}, "test/noparams", "");
    ASSERT_TRUE(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT_TRUE(holds<IncomingRequest>(msg));
    auto& req = get<IncomingRequest>(msg);
    EXPECT_TRUE(req.params.empty());
}

};  // TEST_SUITE(ipc_bincode_codec_parse)

// ============================================================================
// Group 2: Codec — encode/parse roundtrip consistency
// ============================================================================

TEST_SUITE(ipc_json_codec_roundtrip) {

// 2.1 encode_request → parse_message roundtrip
TEST_CASE(request_roundtrip) {
    JsonCodec codec;
    auto encoded =
        codec.encode_request(protocol::RequestID{std::int64_t(99)}, "math/add", R"({"a":1,"b":2})");
    ASSERT_TRUE(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT_TRUE(holds<IncomingRequest>(msg));
    auto& req = get<IncomingRequest>(msg);
    EXPECT_EQ(req.id, protocol::RequestID{std::int64_t(99)});
    EXPECT_EQ(req.method, "math/add");
    EXPECT_FALSE(req.params.empty());
}

// 2.2 encode_notification → parse_message roundtrip
TEST_CASE(notification_roundtrip) {
    JsonCodec codec;
    auto encoded = codec.encode_notification("log/info", R"({"text":"hello"})");
    ASSERT_TRUE(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT_TRUE(holds<IncomingNotification>(msg));
    auto& note = get<IncomingNotification>(msg);
    EXPECT_EQ(note.method, "log/info");
}

// 2.3 encode_success_response → parse_message roundtrip
TEST_CASE(success_response_roundtrip) {
    JsonCodec codec;
    auto encoded =
        codec.encode_success_response(protocol::RequestID{std::int64_t(10)}, R"({"value":42})");
    ASSERT_TRUE(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT_TRUE(holds<IncomingResponse>(msg));
    auto& resp = get<IncomingResponse>(msg);
    EXPECT_EQ(resp.id, protocol::RequestID{std::int64_t(10)});
}

// 2.4 encode_error_response → parse_message roundtrip — Error fields preserved
TEST_CASE(error_response_roundtrip) {
    JsonCodec codec;
    Error original(protocol::ErrorCode::InternalError, "something broke");
    auto encoded = codec.encode_error_response(protocol::RequestID{std::int64_t(20)}, original);
    ASSERT_TRUE(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT_TRUE(holds<IncomingErrorResponse>(msg));
    auto& err = get<IncomingErrorResponse>(msg);
    EXPECT_EQ(err.id, protocol::RequestID{std::int64_t(20)});
    EXPECT_EQ(err.error.code, original.code);
    EXPECT_EQ(err.error.message, original.message);
}

// 2.5 Empty params roundtrip
TEST_CASE(empty_params_roundtrip) {
    JsonCodec codec;
    auto encoded = codec.encode_request(protocol::RequestID{std::int64_t(1)}, "test/empty", "");
    ASSERT_TRUE(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT_TRUE(holds<IncomingRequest>(msg));
}

};  // TEST_SUITE(ipc_json_codec_roundtrip)

TEST_SUITE(ipc_bincode_codec_roundtrip) {

// 2.1 request roundtrip
TEST_CASE(request_roundtrip) {
    BincodeCodec codec;
    auto encoded =
        codec.encode_request(protocol::RequestID{std::int64_t(99)}, "math/add", R"({"a":1})");
    ASSERT_TRUE(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT_TRUE(holds<IncomingRequest>(msg));
    auto& req = get<IncomingRequest>(msg);
    EXPECT_EQ(req.id, protocol::RequestID{std::int64_t(99)});
    EXPECT_EQ(req.method, "math/add");
}

// 2.2 notification roundtrip
TEST_CASE(notification_roundtrip) {
    BincodeCodec codec;
    auto encoded = codec.encode_notification("log/info", R"({"text":"hello"})");
    ASSERT_TRUE(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT_TRUE(holds<IncomingNotification>(msg));
    auto& note = get<IncomingNotification>(msg);
    EXPECT_EQ(note.method, "log/info");
}

// 2.3 success response roundtrip
TEST_CASE(success_response_roundtrip) {
    BincodeCodec codec;
    auto encoded =
        codec.encode_success_response(protocol::RequestID{std::int64_t(10)}, R"({"value":42})");
    ASSERT_TRUE(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT_TRUE(holds<IncomingResponse>(msg));
    auto& resp = get<IncomingResponse>(msg);
    EXPECT_EQ(resp.id, protocol::RequestID{std::int64_t(10)});
}

// 2.4 error response roundtrip
TEST_CASE(error_response_roundtrip) {
    BincodeCodec codec;
    Error original(protocol::ErrorCode::InternalError, "something broke");
    auto encoded = codec.encode_error_response(protocol::RequestID{std::int64_t(20)}, original);
    ASSERT_TRUE(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT_TRUE(holds<IncomingErrorResponse>(msg));
    auto& err = get<IncomingErrorResponse>(msg);
    EXPECT_EQ(err.id, protocol::RequestID{std::int64_t(20)});
    EXPECT_EQ(err.error.code, original.code);
    EXPECT_EQ(err.error.message, original.message);
}

// 2.5 empty params roundtrip
TEST_CASE(empty_params_roundtrip) {
    BincodeCodec codec;
    auto encoded = codec.encode_request(protocol::RequestID{std::int64_t(1)}, "test/empty", "");
    ASSERT_TRUE(encoded.has_value());

    auto msg = codec.parse_message(*encoded);
    ASSERT_TRUE(holds<IncomingRequest>(msg));
    auto& req = get<IncomingRequest>(msg);
    EXPECT_TRUE(req.params.empty());
}

};  // TEST_SUITE(ipc_bincode_codec_roundtrip)

}  // namespace eventide::ipc
