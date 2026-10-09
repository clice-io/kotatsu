#include <cstddef>
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
    test::codec_protocol(test::CodecKit<test::JSONAdapter>{add_case});
}

ZEST_CASE(error_response_roundtrip_keeps_the_data) {
    test::error_response_roundtrip_keeps_the_data<test::JSONAdapter>();
}

ZEST_CASE(encode_error_response_writes_the_data) {
    JSONCodec codec;
    codec::dyn::Value data{
        {"retry", true},
        {"count", 3   },
    };
    auto encoded = codec.encode_error_response(9, Error(ErrorCode::RequestFailed, "fail", data));
    ZASSERT(encoded.has_value());
    auto message = test::JSONAdapter::read(*encoded);
    ZASSERT(message.has_value());
    ZASSERT(message->error.data.has_value());
    ZEXPECT(*message->error.data == data);
}

// Empty params, those of a method that takes none, leave the member out.
ZEST_CASE(encode_without_params_leaves_them_out) {
    JSONCodec codec;
    auto request = codec.encode_request(1, "shutdown", "");
    auto notification = codec.encode_notification("exit", "");
    ZASSERT(request.has_value());
    ZASSERT(notification.has_value());
    ZEXPECT(*request == R"({"jsonrpc":"2.0","id":1,"method":"shutdown"})");
    ZEXPECT(*notification == R"({"jsonrpc":"2.0","method":"exit"})");
}

// The envelope is written around the params or result as they are.
ZEST_CASE(encode_writes_the_members_in_order) {
    JSONCodec codec;
    auto request = codec.encode_request(7, "test/add", R"({"a":1,"b":2})");
    auto notification = codec.encode_notification("test/note", R"({"text":"hi"})");
    auto response = codec.encode_success_response("r\"1", "[1,2]");
    ZASSERT(request.has_value());
    ZASSERT(notification.has_value());
    ZASSERT(response.has_value());
    ZEXPECT(*request == R"({"jsonrpc":"2.0","id":7,"method":"test/add","params":{"a":1,"b":2}})");
    ZEXPECT(*notification == R"({"jsonrpc":"2.0","method":"test/note","params":{"text":"hi"}})");
    ZEXPECT(*response == R"({"jsonrpc":"2.0","id":"r\"1","result":[1,2]})");
}

// A method that returns nothing answers null.
ZEST_CASE(encode_success_response_without_a_result_writes_null) {
    JSONCodec codec;
    auto response = codec.encode_success_response(1, "");
    ZASSERT(response.has_value());
    ZEXPECT(*response == R"({"jsonrpc":"2.0","id":1,"result":null})");
}

ZEST_CASE(encode_escapes_the_method) {
    JSONCodec codec;
    auto request = codec.encode_request(1, R"(say "\hi")", "");
    ZASSERT(request.has_value());
    ZEXPECT(*request == R"({"jsonrpc":"2.0","id":1,"method":"say \"\\hi\""})");
}

ZEST_CASE(encode_with_text_that_is_no_utf8_fails) {
    JSONCodec codec;
    auto request = codec.encode_request(1, "caf\xE9", "");
    auto notification = codec.encode_notification("caf\xE9", "");
    auto response = codec.encode_success_response("caf\xE9", "1");
    for(const auto* encoded: {&request, &notification, &response}) {
        ZASSERT(!encoded->has_value());
        ZEXPECT(code_of(encoded->error()) == ErrorCode::InternalError);
        ZEXPECT(zest::contains(encoded->error().message, "UTF-8"));
    }
}

ZEST_CASE(parse_message_checks_a_string_byte_by_byte) {
    // Strings are scanned eight bytes at a time; what breaks or ends one is
    // found wherever it stands.
    for(std::size_t at = 0; at < 16; ++at) {
        ZEST_CONTEXT("at {}", at);
        auto method = [&](std::string_view inner) {
            return std::string(at, 'm') + std::string(inner) + std::string(16 - at, 'm');
        };
        JSONCodec codec;
        auto with = [&](std::string_view inner) {
            return std::format(R"({{"jsonrpc":"2.0","method":"{}"}})", method(inner));
        };
        auto tab = codec.parse_message(with("\t"));
        auto unknown_escape = codec.parse_message(with(R"(\q)"));
        auto quote = codec.parse_message(with(R"(\")"));
        const auto* tab_failure = std::get_if<IncomingParseError>(&tab);
        const auto* escape_failure = std::get_if<IncomingParseError>(&unknown_escape);
        const auto* notification = std::get_if<IncomingNotification>(&quote);
        ZASSERT(tab_failure != nullptr);
        ZASSERT(escape_failure != nullptr);
        ZASSERT(notification != nullptr);
        ZEXPECT(code_of(tab_failure->error) == ErrorCode::ParseError);
        ZEXPECT(code_of(escape_failure->error) == ErrorCode::ParseError);
        ZEXPECT(notification->method == method("\""));
    }
}

ZEST_CASE(parse_message_keeps_a_payload_with_room_in_place) {
    JSONCodec codec;
    std::string payload = R"({"jsonrpc":"2.0","id":1,"method":"test/add","params":{"a":1}})";
    payload.reserve(payload.size() + 64);
    const auto* data = payload.data();
    const auto capacity = payload.capacity();
    const auto before = payload;
    auto parsed = codec.parse_message(payload);
    ZEXPECT(std::holds_alternative<IncomingRequest>(parsed));
    ZEXPECT(payload == before);
    ZEXPECT(static_cast<const void*>(payload.data()) == static_cast<const void*>(data));
    ZEXPECT(payload.capacity() == capacity);
}

// JSON-RPC lets an error without data leave the member out.
ZEST_CASE(encode_error_response_without_data_leaves_it_out) {
    JSONCodec codec;
    auto encoded = codec.encode_error_response(9, Error(ErrorCode::RequestFailed, "fail"));
    ZASSERT(encoded.has_value());
    ZEXPECT(*encoded == R"({"jsonrpc":"2.0","id":9,"error":{"code":-32000,"message":"fail"}})");
}

// Data that is null is data all the same.
ZEST_CASE(encode_error_response_writes_null_data) {
    JSONCodec codec;
    auto encoded = codec.encode_error_response(
        9,
        Error(ErrorCode::RequestFailed, "fail", codec::dyn::Value(nullptr)));
    ZASSERT(encoded.has_value());
    ZEXPECT(*encoded ==
            R"({"jsonrpc":"2.0","id":9,"error":{"code":-32000,"message":"fail","data":null}})");
}

ZEST_CASE(parse_message_reads_error_data) {
    JSONCodec codec;
    auto parsed = codec.parse_message(
        R"({"jsonrpc":"2.0","id":9,"error":{"code":-32000,"message":"fail","data":{"retry":true,"count":3}}})");
    const auto* response = std::get_if<IncomingErrorResponse>(&parsed);
    ZASSERT(response != nullptr);
    ZASSERT(response->error.data.has_value());
    ZEXPECT(*response->error.data == codec::dyn::Value{
                                         {"retry", true},
                                         {"count", 3   },
    });
}

ZEST_CASE(request_without_params_reads_empty_params) {
    JSONCodec codec;
    auto parsed = codec.parse_message(R"({"jsonrpc":"2.0","id":1,"method":"test/noparams"})");
    const auto* request = std::get_if<IncomingRequest>(&parsed);
    ZASSERT(request != nullptr);
    ZEXPECT(request->method == "test/noparams");
    ZEXPECT(request->params.empty());
}

// shutdown and exit take no params; clients send them as null, or not at all.
ZEST_CASE(null_params_read_as_no_params) {
    JSONCodec codec;
    auto parsed =
        codec.parse_message(R"({"jsonrpc":"2.0","id":1,"method":"shutdown","params":null})");
    const auto* request = std::get_if<IncomingRequest>(&parsed);
    ZASSERT(request != nullptr);
    ZEXPECT(request->params.empty());
    ZEXPECT(codec.deserialize_value<test::EmptyParams>(request->params).has_value());
}

ZEST_CASE(null_result_is_a_result) {
    JSONCodec codec;
    auto parsed = codec.parse_message(R"({"jsonrpc":"2.0","id":3,"result":null})");
    const auto* response = std::get_if<IncomingResponse>(&parsed);
    ZASSERT(response != nullptr);
    ZEXPECT(response->result == "null");
}

ZEST_CASE(response_with_both_result_and_error_is_an_invalid_response) {
    JSONCodec codec;
    auto parsed = codec.parse_message(
        R"({"jsonrpc":"2.0","id":5,"result":{"x":1},"error":{"code":-1,"message":"oops"}})");
    const auto* response = std::get_if<IncomingErrorResponse>(&parsed);
    ZASSERT(response != nullptr);
    ZEXPECT(response->id == protocol::RequestID(5));
    ZEXPECT(code_of(response->error) == ErrorCode::InvalidRequest);
}

ZEST_CASE(response_with_neither_result_nor_error_is_an_invalid_response) {
    JSONCodec codec;
    auto parsed = codec.parse_message(R"({"jsonrpc":"2.0","id":5})");
    const auto* response = std::get_if<IncomingErrorResponse>(&parsed);
    ZASSERT(response != nullptr);
    ZEXPECT(response->id == protocol::RequestID(5));
    ZEXPECT(code_of(response->error) == ErrorCode::InvalidRequest);
}

ZEST_CASE(object_without_method_or_id_is_an_invalid_request) {
    JSONCodec codec;
    auto parsed = codec.parse_message("{}");
    const auto* failure = std::get_if<IncomingParseError>(&parsed);
    ZASSERT(failure != nullptr);
    ZEXPECT(!failure->id.has_value());
    ZEXPECT(code_of(failure->error) == ErrorCode::InvalidRequest);
}

ZEST_CASE(request_with_a_null_id_is_an_invalid_request) {
    JSONCodec codec;
    auto parsed = codec.parse_message(R"({"jsonrpc":"2.0","id":null,"method":"test/add"})");
    const auto* failure = std::get_if<IncomingParseError>(&parsed);
    ZASSERT(failure != nullptr);
    ZEXPECT(!failure->id.has_value());
    ZEXPECT(code_of(failure->error) == ErrorCode::InvalidRequest);
}

ZEST_CASE(result_with_a_null_id_answers_no_request) {
    JSONCodec codec;
    auto parsed = codec.parse_message(R"({"jsonrpc":"2.0","id":null,"result":1})");
    const auto* response = std::get_if<IncomingErrorResponse>(&parsed);
    ZASSERT(response != nullptr);
    ZEXPECT(!response->id.has_value());
}

ZEST_CASE(json_that_is_no_message_is_an_invalid_request) {
    JSONCodec codec;
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
        ZASSERT(failure != nullptr);
        ZEXPECT(!failure->id.has_value());
        ZEXPECT(code_of(failure->error) == ErrorCode::InvalidRequest);
    }
}

// JSON has numbers of any size, which simdjson does not read.
ZEST_CASE(json_with_numbers_past_64_bits_is_an_invalid_request) {
    JSONCodec codec;
    for(std::string_view payload: {
            "-9223372036854776000",
            "[18446744073709551616]",
            R"([{"":1e400}])",
            R"({"jsonrpc":"2.0","method":99999999999999999999999})",
        }) {
        ZEST_CONTEXT("payload: {}", payload);
        auto parsed = codec.parse_message(payload);
        const auto* failure = std::get_if<IncomingParseError>(&parsed);
        ZASSERT(failure != nullptr);
        ZEXPECT(!failure->id.has_value());
        ZEXPECT(code_of(failure->error) == ErrorCode::InvalidRequest);
    }
}

ZEST_CASE(text_that_is_no_json_is_a_parse_error) {
    JSONCodec codec;
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
        ZASSERT(failure != nullptr);
        ZEXPECT(!failure->id.has_value());
        ZEXPECT(code_of(failure->error) == ErrorCode::ParseError);
    }
}

ZEST_CASE(request_with_a_malformed_member_keeps_its_id) {
    JSONCodec codec;
    auto parsed = codec.parse_message(R"({"jsonrpc":"2.0","id":5,"method":7})");
    const auto* failure = std::get_if<IncomingParseError>(&parsed);
    ZASSERT(failure != nullptr);
    ZEXPECT(failure->id == protocol::RequestID(5));
    ZEXPECT(code_of(failure->error) == ErrorCode::InvalidRequest);
}

ZEST_CASE(response_with_a_malformed_error_keeps_its_id) {
    JSONCodec codec;
    for(std::string_view payload: {
            R"({"jsonrpc":"2.0","id":1,"error":{"code":"E1","message":"x"}})",
            R"({"jsonrpc":"2.0","id":1,"error":"x"})",
        }) {
        ZEST_CONTEXT("payload: {}", payload);
        auto parsed = codec.parse_message(payload);
        const auto* response = std::get_if<IncomingErrorResponse>(&parsed);
        ZASSERT(response != nullptr);
        ZEXPECT(response->id == protocol::RequestID(1));
        ZEXPECT(code_of(response->error) == ErrorCode::InvalidRequest);
    }
}

// JSON-RPC 2.0 requires "jsonrpc" to be exactly "2.0".
ZEST_CASE(request_without_jsonrpc_2_0_is_an_invalid_request) {
    JSONCodec codec;
    for(std::string_view payload: {
            R"({"id":5,"method":"test/echo","params":[]})",
            R"({"jsonrpc":"1.0","id":5,"method":"test/echo","params":[]})",
            R"({"jsonrpc":2,"id":5,"method":"test/echo","params":[]})",
        }) {
        ZEST_CONTEXT("payload: {}", payload);
        auto parsed = codec.parse_message(payload);
        const auto* failure = std::get_if<IncomingParseError>(&parsed);
        ZASSERT(failure != nullptr);
        ZEXPECT(!failure->notification);
        ZEXPECT(failure->id == protocol::RequestID(5));
        ZEXPECT(code_of(failure->error) == ErrorCode::InvalidRequest);
    }
}

ZEST_CASE(notification_without_jsonrpc_2_0_is_never_answered) {
    JSONCodec codec;
    for(std::string_view payload: {
            R"({"method":"test/note","params":{}})",
            R"({"jsonrpc":"1.0","method":"test/note","params":{}})",
            R"({"jsonrpc":2,"method":"test/note","params":{}})",
        }) {
        ZEST_CONTEXT("payload: {}", payload);
        auto parsed = codec.parse_message(payload);
        const auto* failure = std::get_if<IncomingParseError>(&parsed);
        ZASSERT(failure != nullptr);
        ZEXPECT(failure->notification);
        ZEXPECT(code_of(failure->error) == ErrorCode::InvalidRequest);
    }
}

ZEST_CASE(response_without_jsonrpc_2_0_fails_its_request) {
    JSONCodec codec;
    auto parsed = codec.parse_message(R"({"id":3,"result":7})");
    const auto* response = std::get_if<IncomingErrorResponse>(&parsed);
    ZASSERT(response != nullptr);
    ZEXPECT(response->id == protocol::RequestID(3));
    ZEXPECT(code_of(response->error) == ErrorCode::InvalidRequest);
}

// JSON-RPC requires an error object's code and message.
ZEST_CASE(error_without_its_code_or_message_fails_its_request) {
    JSONCodec codec;
    for(std::string_view payload: {
            R"({"jsonrpc":"2.0","id":1,"error":{"message":"x"}})",
            R"({"jsonrpc":"2.0","id":1,"error":{"code":-32000}})",
        }) {
        ZEST_CONTEXT("payload: {}", payload);
        auto parsed = codec.parse_message(payload);
        const auto* response = std::get_if<IncomingErrorResponse>(&parsed);
        ZASSERT(response != nullptr);
        ZEXPECT(response->id == protocol::RequestID(1));
        ZEXPECT(code_of(response->error) == ErrorCode::InvalidRequest);
    }
}

// Reading a value recurses once per level, so a message nested deeper than
// the codec reads is judged without reading its values; at 50000 levels,
// reading it would overflow the stack.
ZEST_CASE(deeply_nested_array_is_an_invalid_request) {
    JSONCodec codec;
    for(std::size_t depth: {2000U, 50000U}) {
        ZEST_CONTEXT("depth: {}", depth);
        auto parsed = codec.parse_message(std::string(depth, '[') + std::string(depth, ']'));
        const auto* failure = std::get_if<IncomingParseError>(&parsed);
        ZASSERT(failure != nullptr);
        ZEXPECT(!failure->id.has_value());
        ZEXPECT(code_of(failure->error) == ErrorCode::InvalidRequest);
    }
}

ZEST_CASE(deeply_nested_notification_is_never_answered) {
    JSONCodec codec;
    auto deep = std::string(50000, '[') + std::string(50000, ']');
    auto parsed = codec.parse_message(
        std::format(R"({{"jsonrpc":"2.0","method":"test/note","params":{}}})", deep));
    const auto* failure = std::get_if<IncomingParseError>(&parsed);
    ZASSERT(failure != nullptr);
    ZEXPECT(failure->notification);
    ZEXPECT(!failure->id.has_value());
}

ZEST_CASE(request_with_deeply_nested_params_is_invalid_under_its_id) {
    JSONCodec codec;
    auto deep = std::string(50000, '[') + std::string(50000, ']');
    auto parsed = codec.parse_message(
        std::format(R"({{"jsonrpc":"2.0","id":7,"method":"test/echo","params":{}}})", deep));
    const auto* failure = std::get_if<IncomingParseError>(&parsed);
    ZASSERT(failure != nullptr);
    ZEXPECT(failure->id == protocol::RequestID(7));
    ZEXPECT(code_of(failure->error) == ErrorCode::InvalidRequest);
}

ZEST_CASE(response_with_deeply_nested_error_data_fails_its_request) {
    JSONCodec codec;
    auto deep = std::string(50000, '[') + std::string(50000, ']');
    auto parsed = codec.parse_message(
        std::format(R"({{"jsonrpc":"2.0","id":3,"error":{{"code":1,"message":"m","data":{}}}}})",
                    deep));
    const auto* response = std::get_if<IncomingErrorResponse>(&parsed);
    ZASSERT(response != nullptr);
    ZEXPECT(response->id == protocol::RequestID(3));
    ZEXPECT(code_of(response->error) == ErrorCode::InvalidRequest);
}

// A key with escapes names the member it spells.
ZEST_CASE(escaped_member_names_are_read) {
    JSONCodec codec;
    auto head = codec.peek(R"({"jsonrpc":"2.0","\u0069d":3,"m\u0065thod":"test/echo","params":[)");
    ZEXPECT(head.kind == MessageHead::Kind::Request);
    ZEXPECT(head.id == protocol::RequestID(3));

    auto deep = std::string(50000, '[') + std::string(50000, ']');
    auto parsed = codec.parse_message(
        std::format(R"({{"jsonrpc":"2.0","\u0069d":7,"m\u0065thod":"test/echo","params":{}}})",
                    deep));
    const auto* failure = std::get_if<IncomingParseError>(&parsed);
    ZASSERT(failure != nullptr);
    ZEXPECT(!failure->notification);
    ZEXPECT(failure->id == protocol::RequestID(7));
}

// Which request a response answers is unknown until its id is read.
ZEST_CASE(peek_of_a_response_whose_id_is_past_the_prefix_knows_no_kind) {
    JSONCodec codec;
    auto head = codec.peek(R"({"jsonrpc":"2.0","result":["a very long)");
    ZEXPECT(head.kind == MessageHead::Kind::Unknown);
    auto null_id = codec.peek(R"({"jsonrpc":"2.0","id":null,"error":{"code":1,"message":"a very)");
    ZEXPECT(null_id.kind == MessageHead::Kind::Response);
    ZEXPECT(!null_id.id.has_value());
}

// JSON-RPC's invalid request objects: an id that is no integer or string
// makes no notification, nor does a method that is no string.
ZEST_CASE(malformed_request_objects_are_answered) {
    JSONCodec codec;
    auto deep = std::string(50000, '[') + std::string(50000, ']');
    for(auto head: {R"("id":true,"method":"test/echo")", R"("method":5)"}) {
        auto payload = std::format(R"({{"jsonrpc":"2.0",{},"params":{}}})", head, deep);
        ZEST_CONTEXT("head: {}", head);
        auto parsed = codec.parse_message(payload);
        const auto* failure = std::get_if<IncomingParseError>(&parsed);
        ZASSERT(failure != nullptr);
        ZEXPECT(!failure->notification);
        ZEXPECT(!failure->id.has_value());
        ZEXPECT(code_of(failure->error) == ErrorCode::InvalidRequest);
    }
}

// simdjson does not check the members it skips; the grammar is checked
// first.
ZEST_CASE(invalid_member_the_envelope_skips_is_a_parse_error) {
    JSONCodec codec;
    auto parsed = codec.parse_message(R"({"jsonrpc":"2.0","id":1,"method":"x","extra":tru})");
    const auto* failure = std::get_if<IncomingParseError>(&parsed);
    ZASSERT(failure != nullptr);
    ZEXPECT(!failure->id.has_value());
    ZEXPECT(code_of(failure->error) == ErrorCode::ParseError);
}

// Brackets inside a string are text, not nesting.
ZEST_CASE(brackets_inside_strings_do_not_nest) {
    JSONCodec codec;
    auto parsed = codec.parse_message(
        std::format(R"({{"jsonrpc":"2.0","id":1,"method":"test/echo","params":["{}\"{}"]}})",
                    std::string(1000, '['),
                    std::string(1000, '{')));
    ZEXPECT(std::holds_alternative<IncomingRequest>(parsed));
}

};  // ZEST_SUITE(ipc_codec_json)

}  // namespace

}  // namespace kota::ipc
