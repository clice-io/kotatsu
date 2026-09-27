#include <format>
#include <string>
#include <string_view>
#include <variant>

#include "ipc/harness/codec_json.h"
#include "ipc/harness/codec_kit.h"
#include "kota/ipc/codec/json.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/codec/dyn/dyn.h"

namespace kota::ipc {

namespace {

using protocol::ErrorCode;
using test::code_of;

ZEST_SUITE(ipc_codec_json) {

ZEST_CASE_GROUP(protocol) {
    test::codec_protocol(test::CodecKit<test::JsonAdapter>{add_case});
}

ZEST_CASE(error_response_roundtrip_keeps_the_data) {
    test::error_response_roundtrip_keeps_the_data<test::JsonAdapter>();
}

ZEST_CASE(encode_error_response_writes_the_data) {
    JsonCodec codec;
    codec::dyn::Value data{
        {"retry", true},
        {"count", 3   },
    };
    auto encoded = codec.encode_error_response(9, Error(ErrorCode::RequestFailed, "fail", data));
    ASSERT(encoded.has_value());
    auto message = test::JsonAdapter::read(*encoded);
    ASSERT(message.has_value());
    ASSERT(message->error.data.has_value());
    EXPECT(*message->error.data == data);
}

ZEST_CASE(parse_message_reads_error_data) {
    JsonCodec codec;
    auto parsed = codec.parse_message(
        R"({"jsonrpc":"2.0","id":9,"error":{"code":-32000,"message":"fail","data":{"retry":true,"count":3}}})");
    const auto* response = std::get_if<IncomingErrorResponse>(&parsed);
    ASSERT(response != nullptr);
    ASSERT(response->error.data.has_value());
    EXPECT(*response->error.data == codec::dyn::Value{
                                        {"retry", true},
                                        {"count", 3   },
    });
}

ZEST_CASE(request_without_params_reads_empty_params) {
    JsonCodec codec;
    auto parsed = codec.parse_message(R"({"jsonrpc":"2.0","id":1,"method":"test/noparams"})");
    const auto* request = std::get_if<IncomingRequest>(&parsed);
    ASSERT(request != nullptr);
    EXPECT(request->method == "test/noparams");
    EXPECT(request->params.empty());
}

// shutdown and exit take no params; clients send them as null, or not at all.
ZEST_CASE(null_params_read_as_no_params) {
    JsonCodec codec;
    auto parsed =
        codec.parse_message(R"({"jsonrpc":"2.0","id":1,"method":"shutdown","params":null})");
    const auto* request = std::get_if<IncomingRequest>(&parsed);
    ASSERT(request != nullptr);
    EXPECT(request->params.empty());
    EXPECT(codec.deserialize_value<test::EmptyParams>(request->params).has_value());
}

ZEST_CASE(null_result_is_a_result) {
    JsonCodec codec;
    auto parsed = codec.parse_message(R"({"jsonrpc":"2.0","id":3,"result":null})");
    const auto* response = std::get_if<IncomingResponse>(&parsed);
    ASSERT(response != nullptr);
    EXPECT(response->result == "null");
}

ZEST_CASE(response_with_both_result_and_error_is_an_invalid_response) {
    JsonCodec codec;
    auto parsed = codec.parse_message(
        R"({"jsonrpc":"2.0","id":5,"result":{"x":1},"error":{"code":-1,"message":"oops"}})");
    const auto* response = std::get_if<IncomingErrorResponse>(&parsed);
    ASSERT(response != nullptr);
    EXPECT(response->id == protocol::RequestID(5));
    EXPECT(code_of(response->error) == ErrorCode::InvalidRequest);
}

ZEST_CASE(response_with_neither_result_nor_error_is_an_invalid_response) {
    JsonCodec codec;
    auto parsed = codec.parse_message(R"({"jsonrpc":"2.0","id":5})");
    const auto* response = std::get_if<IncomingErrorResponse>(&parsed);
    ASSERT(response != nullptr);
    EXPECT(response->id == protocol::RequestID(5));
    EXPECT(code_of(response->error) == ErrorCode::InvalidRequest);
}

ZEST_CASE(object_without_method_or_id_is_an_invalid_request) {
    JsonCodec codec;
    auto parsed = codec.parse_message("{}");
    const auto* failure = std::get_if<IncomingParseError>(&parsed);
    ASSERT(failure != nullptr);
    EXPECT(!failure->id.has_value());
    EXPECT(code_of(failure->error) == ErrorCode::InvalidRequest);
}

ZEST_CASE(request_with_a_null_id_is_an_invalid_request) {
    JsonCodec codec;
    auto parsed = codec.parse_message(R"({"jsonrpc":"2.0","id":null,"method":"test/add"})");
    const auto* failure = std::get_if<IncomingParseError>(&parsed);
    ASSERT(failure != nullptr);
    EXPECT(!failure->id.has_value());
    EXPECT(code_of(failure->error) == ErrorCode::InvalidRequest);
}

ZEST_CASE(result_with_a_null_id_answers_no_request) {
    JsonCodec codec;
    auto parsed = codec.parse_message(R"({"jsonrpc":"2.0","id":null,"result":1})");
    const auto* response = std::get_if<IncomingErrorResponse>(&parsed);
    ASSERT(response != nullptr);
    EXPECT(!response->id.has_value());
}

ZEST_CASE(json_that_is_no_message_is_an_invalid_request) {
    JsonCodec codec;
    for(std::string_view payload: {
            "42",
            R"([{"jsonrpc":"2.0","method":"test/note"}])",
            R"({"jsonrpc":"2.0","id":true,"method":"test/add"})",
            R"({"jsonrpc":"2.0","id":1.5,"method":"test/add"})",
            R"({"jsonrpc":"2.0","method":5})",
        }) {
        ZEST_CONTEXT("payload: {}", payload);
        auto parsed = codec.parse_message(payload);
        const auto* failure = std::get_if<IncomingParseError>(&parsed);
        ASSERT(failure != nullptr);
        EXPECT(!failure->id.has_value());
        EXPECT(code_of(failure->error) == ErrorCode::InvalidRequest);
    }
}

// JSON has numbers of any size, which simdjson does not read.
ZEST_CASE(json_with_numbers_past_64_bits_is_an_invalid_request) {
    JsonCodec codec;
    for(std::string_view payload: {
            "-9223372036854776000",
            "[18446744073709551616]",
            R"([{"":1e400}])",
            R"({"jsonrpc":"2.0","method":99999999999999999999999})",
        }) {
        ZEST_CONTEXT("payload: {}", payload);
        auto parsed = codec.parse_message(payload);
        const auto* failure = std::get_if<IncomingParseError>(&parsed);
        ASSERT(failure != nullptr);
        EXPECT(!failure->id.has_value());
        EXPECT(code_of(failure->error) == ErrorCode::InvalidRequest);
    }
}

ZEST_CASE(text_that_is_no_json_is_a_parse_error) {
    JsonCodec codec;
    for(std::string_view payload: {
            "",
            " ",
            "01",
            "-",
            "1.",
            "1e",
            "tru",
            "[1,]",
            "[1 2]",
            "{,}",
            R"({"a" 1})",
            R"({"a":1,})",
            R"({"a":1}x)",
            R"(["\x"])",
            R"(["\u12"])",
            "[\"\t\"]",
            "[\"\xff\"]",
            "[[]",
        }) {
        ZEST_CONTEXT("payload: {}", payload);
        auto parsed = codec.parse_message(payload);
        const auto* failure = std::get_if<IncomingParseError>(&parsed);
        ASSERT(failure != nullptr);
        EXPECT(!failure->id.has_value());
        EXPECT(code_of(failure->error) == ErrorCode::ParseError);
    }
}

ZEST_CASE(request_with_a_malformed_member_keeps_its_id) {
    JsonCodec codec;
    auto parsed = codec.parse_message(R"({"jsonrpc":"2.0","id":5,"method":7})");
    const auto* failure = std::get_if<IncomingParseError>(&parsed);
    ASSERT(failure != nullptr);
    EXPECT(failure->id == protocol::RequestID(5));
    EXPECT(code_of(failure->error) == ErrorCode::InvalidRequest);
}

ZEST_CASE(response_with_a_malformed_error_keeps_its_id) {
    JsonCodec codec;
    for(std::string_view payload: {
            R"({"jsonrpc":"2.0","id":1,"error":{"code":"E1","message":"x"}})",
            R"({"jsonrpc":"2.0","id":1,"error":"x"})",
        }) {
        ZEST_CONTEXT("payload: {}", payload);
        auto parsed = codec.parse_message(payload);
        const auto* response = std::get_if<IncomingErrorResponse>(&parsed);
        ASSERT(response != nullptr);
        EXPECT(response->id == protocol::RequestID(1));
        EXPECT(code_of(response->error) == ErrorCode::InvalidRequest);
    }
}

// Reading a value recurses once per level, so a message nested deeper than
// the codec reads is judged without reading its values; at 50000 levels,
// reading it would overflow the stack.
ZEST_CASE(deeply_nested_array_is_an_invalid_request) {
    JsonCodec codec;
    for(std::size_t depth: {2000U, 50000U}) {
        ZEST_CONTEXT("depth: {}", depth);
        auto parsed = codec.parse_message(std::string(depth, '[') + std::string(depth, ']'));
        const auto* failure = std::get_if<IncomingParseError>(&parsed);
        ASSERT(failure != nullptr);
        EXPECT(!failure->id.has_value());
        EXPECT(code_of(failure->error) == ErrorCode::InvalidRequest);
    }
}

ZEST_CASE(request_with_deeply_nested_params_is_invalid_under_its_id) {
    JsonCodec codec;
    auto deep = std::string(50000, '[') + std::string(50000, ']');
    auto parsed = codec.parse_message(
        std::format(R"({{"jsonrpc":"2.0","id":7,"method":"test/echo","params":{}}})", deep));
    const auto* failure = std::get_if<IncomingParseError>(&parsed);
    ASSERT(failure != nullptr);
    EXPECT(failure->id == protocol::RequestID(7));
    EXPECT(code_of(failure->error) == ErrorCode::InvalidRequest);
}

ZEST_CASE(response_with_deeply_nested_error_data_fails_its_request) {
    JsonCodec codec;
    auto deep = std::string(50000, '[') + std::string(50000, ']');
    auto parsed = codec.parse_message(
        std::format(R"({{"jsonrpc":"2.0","id":3,"error":{{"code":1,"message":"m","data":{}}}}})",
                    deep));
    const auto* response = std::get_if<IncomingErrorResponse>(&parsed);
    ASSERT(response != nullptr);
    EXPECT(response->id == protocol::RequestID(3));
    EXPECT(code_of(response->error) == ErrorCode::InvalidRequest);
}

// Brackets inside a string are text, not nesting.
ZEST_CASE(brackets_inside_strings_do_not_nest) {
    JsonCodec codec;
    auto parsed = codec.parse_message(
        std::format(R"({{"jsonrpc":"2.0","id":1,"method":"test/echo","params":["{}\"{}"]}})",
                    std::string(1000, '['),
                    std::string(1000, '{')));
    EXPECT(std::holds_alternative<IncomingRequest>(parsed));
}

};  // ZEST_SUITE(ipc_codec_json)

}  // namespace

}  // namespace kota::ipc
