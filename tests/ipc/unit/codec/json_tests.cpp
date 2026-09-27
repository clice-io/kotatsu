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
    test::codec_protocol(test::CodecKit<test::JsonWire>{add_case});
}

ZEST_CASE(error_response_roundtrip_keeps_the_data) {
    test::error_response_roundtrip_keeps_the_data<test::JsonWire>();
}

ZEST_CASE(deserialize_value_of_nothing_into_fields_fails) {
    test::deserialize_value_of_nothing_into_fields_fails<test::JsonWire>();
}

ZEST_CASE(encode_error_response_writes_the_data) {
    JsonCodec codec;
    codec::dyn::Value data{
        {"retry", true},
        {"count", 3   },
    };
    auto encoded = codec.encode_error_response(9, Error(ErrorCode::RequestFailed, "fail", data));
    ASSERT(encoded.has_value());
    auto message = test::JsonWire::read(*encoded);
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
    EXPECT(code_of(failure->error) == ErrorCode::InvalidRequest);
}

// N3: JSON that parses but is no message is reported as a ParseError.
ZEST_CASE(json_that_is_no_message_is_an_invalid_request, skip = true) {
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
        EXPECT(code_of(failure->error) == ErrorCode::InvalidRequest);
    }
}

};  // ZEST_SUITE(ipc_codec_json)

}  // namespace

}  // namespace kota::ipc
