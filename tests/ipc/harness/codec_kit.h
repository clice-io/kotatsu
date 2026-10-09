#pragma once

// The shared suite of the ipc codecs. JSONCodec and BincodeCodec implement one
// duck-typed protocol (parse_message, encode_*, serialize_value,
// deserialize_value), so its behaviour is written once, below, and each codec
// runs it through an adapter (codec_json.h, codec_bincode.h):
//
//     ZEST_CASE_GROUP(protocol) {
//         test::codec_protocol(test::CodecKit<test::JSONAdapter>{add_case});
//     }
//
// The adapter writes and reads messages without the codec under test: json
// text by hand, bincode through mirrors of its envelopes. So a case either has
// the codec encode and the adapter read, or the adapter write and the codec
// parse. The Peer suite (peer_fixture.h) plays the remote through the same
// adapters.
//
// A case whose expectation the library does not meet yet is not registered
// here but written as a function template below the area, which each codec's
// file runs as a ZEST_CASE, `skip = true` where the codec has the bug, with a
// comment naming it. Its fix removes the skip.

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "ipc/harness/fixtures.h"
#include "kota/ipc/codec.h"
#include "kota/zest/zest.h"
#include "kota/codec/dyn/dyn.h"

namespace kota::test {

using ipc::protocol::RequestID;

/// What a codec's messages can carry. Declared by the adapter rather than
/// derived from the library, so the library does not judge itself.
struct Caps {
    /// A request id may be a string; otherwise ids are integers only.
    bool string_ids = false;
};

/// A message as the remote reads it, whatever the codec.
struct Message {
    enum class Kind : std::uint8_t {
        Request,
        Notification,
        Result,
        Error,
    };

    Kind kind = Kind::Notification;
    /// Absent from a notification, and from an error that answers no
    /// request.
    std::optional<RequestID> id;
    /// A request's or notification's method.
    std::string method;
    /// A request's or notification's params, or a result, in the codec's
    /// encoding.
    std::string body;
    /// An error response's error.
    ipc::Error error;
};

/// A codec adapter: the codec, its name and caps, values in its encoding,
/// the messages a remote sends and a reader for those the codec writes.
template <typename A>
concept CodecAdapter = requires(const RequestID& id,
                                const std::optional<RequestID>& maybe_id,
                                std::string_view text,
                                const ipc::Error& error) {
    typename A::Codec;
    { A::name } -> std::convertible_to<std::string_view>;
    { A::caps } -> std::convertible_to<Caps>;
    /// A payload that is no message.
    { A::garbage } -> std::convertible_to<std::string_view>;
    /// A body no struct with fields decodes from.
    { A::not_a_value } -> std::convertible_to<std::string_view>;
    { A::encode(AddParams{}) } -> std::same_as<std::string>;
    { A::template decode<AddParams>(text) } -> std::same_as<std::optional<AddParams>>;
    { A::request_raw(id, text, text) } -> std::same_as<std::string>;
    { A::notification_raw(text, text) } -> std::same_as<std::string>;
    { A::response_raw(id, text) } -> std::same_as<std::string>;
    { A::error_response(maybe_id, error) } -> std::same_as<std::string>;
    { A::read(text) } -> std::same_as<std::expected<Message, std::string>>;
};

/// A request for `method` with `params`, as the remote sends it.
template <CodecAdapter A, typename T>
std::string request(const RequestID& id, std::string_view method, const T& params) {
    return A::request_raw(id, method, A::encode(params));
}

/// A notification for `method` with `params`, as the remote sends it.
template <CodecAdapter A, typename T>
std::string notification(std::string_view method, const T& params) {
    return A::notification_raw(method, A::encode(params));
}

/// A success response carrying `result`, as the remote sends it.
template <CodecAdapter A, typename T>
std::string response(const RequestID& id, const T& result) {
    return A::response_raw(id, A::encode(result));
}

/// `body`, a message's params or result, decoded as T.
template <typename T, CodecAdapter A>
std::optional<T> decoded(std::string_view body) {
    return A::template decode<T>(body);
}

/// The code of `error`, as the enumerator it matches.
inline ipc::protocol::ErrorCode code_of(const ipc::Error& error) {
    return static_cast<ipc::protocol::ErrorCode>(error.code);
}

/// Where the codec cases of adapter A are registered.
template <CodecAdapter A>
struct CodecKit {
    const zest::CaseRegistrar& add;
};

template <CodecAdapter A>
void codec_protocol(const CodecKit<A>& kit) {
    using ipc::protocol::ErrorCode;
    using Codec = typename A::Codec;

    kit.add("encode_request_writes_a_request", [] {
        Codec codec;
        auto encoded = codec.encode_request(7, "test/add", A::encode(AddParams{.a = 1, .b = 2}));
        ZASSERT(encoded.has_value());
        auto message = A::read(*encoded);
        ZASSERT(message.has_value());
        ZEXPECT(message->kind == Message::Kind::Request);
        ZEXPECT(message->id == RequestID(7));
        ZEXPECT(message->method == "test/add");
        auto params = decoded<AddParams, A>(message->body);
        ZASSERT(params.has_value());
        ZEXPECT(*params == AddParams{.a = 1, .b = 2});
    });

    if constexpr(A::caps.string_ids) {
        kit.add("encode_request_writes_a_string_id", [] {
            Codec codec;
            auto encoded = codec.encode_request("abc", "test/add", A::encode(AddParams{}));
            ZASSERT(encoded.has_value());
            auto message = A::read(*encoded);
            ZASSERT(message.has_value());
            ZEXPECT(message->id == RequestID("abc"));
        });
    } else {
        kit.add("encode_request_with_a_string_id_fails", [] {
            Codec codec;
            auto encoded = codec.encode_request("abc", "test/add", A::encode(AddParams{}));
            ZASSERT(!encoded.has_value());
            ZEXPECT(code_of(encoded.error()) == ErrorCode::InternalError);
        });

        kit.add("encode_success_response_with_a_string_id_fails", [] {
            Codec codec;
            auto encoded = codec.encode_success_response("abc", A::encode(AddResult{.sum = 1}));
            ZASSERT(!encoded.has_value());
            ZEXPECT(code_of(encoded.error()) == ErrorCode::InternalError);
        });
    }

    kit.add("encode_notification_writes_a_notification", [] {
        Codec codec;
        auto encoded = codec.encode_notification("test/note", A::encode(NoteParams{.text = "hi"}));
        ZASSERT(encoded.has_value());
        auto message = A::read(*encoded);
        ZASSERT(message.has_value());
        ZEXPECT(message->kind == Message::Kind::Notification);
        ZEXPECT(!message->id.has_value());
        ZEXPECT(message->method == "test/note");
        auto params = decoded<NoteParams, A>(message->body);
        ZASSERT(params.has_value());
        ZEXPECT(params->text == "hi");
    });

    kit.add("encode_success_response_writes_a_result", [] {
        Codec codec;
        auto encoded = codec.encode_success_response(42, A::encode(AddResult{.sum = 3}));
        ZASSERT(encoded.has_value());
        auto message = A::read(*encoded);
        ZASSERT(message.has_value());
        ZEXPECT(message->kind == Message::Kind::Result);
        ZEXPECT(message->id == RequestID(42));
        auto result = decoded<AddResult, A>(message->body);
        ZASSERT(result.has_value());
        ZEXPECT(result->sum == 3);
    });

    kit.add("encode_error_response_writes_an_error", [] {
        Codec codec;
        auto encoded =
            codec.encode_error_response(20,
                                        ipc::Error(ErrorCode::InternalError, "something broke"));
        ZASSERT(encoded.has_value());
        auto message = A::read(*encoded);
        ZASSERT(message.has_value());
        ZEXPECT(message->kind == Message::Kind::Error);
        ZEXPECT(message->id == RequestID(20));
        ZEXPECT(code_of(message->error) == ErrorCode::InternalError);
        ZEXPECT(message->error.message == "something broke");
    });

    kit.add("encode_error_response_without_an_id_writes_none", [] {
        Codec codec;
        auto encoded =
            codec.encode_error_response(std::nullopt, ipc::Error(ErrorCode::ParseError, "bad"));
        ZASSERT(encoded.has_value());
        auto message = A::read(*encoded);
        ZASSERT(message.has_value());
        ZEXPECT(message->kind == Message::Kind::Error);
        ZEXPECT(!message->id.has_value());
        ZEXPECT(code_of(message->error) == ErrorCode::ParseError);
    });

    kit.add("parse_message_reads_a_request", [] {
        Codec codec;
        auto params = A::encode(AddParams{.a = 1, .b = 2});
        auto parsed = codec.parse_message(A::request_raw(99, "math/add", params));
        const auto* request = std::get_if<ipc::IncomingRequest>(&parsed);
        ZASSERT(request != nullptr);
        ZEXPECT(request->id == RequestID(99));
        ZEXPECT(request->method == "math/add");
        ZEXPECT(request->params == params);
    });

    if constexpr(A::caps.string_ids) {
        kit.add("parse_message_reads_a_string_id", [] {
            Codec codec;
            auto parsed = codec.parse_message(request<A>("abc", "test/add", AddParams{}));
            const auto* request = std::get_if<ipc::IncomingRequest>(&parsed);
            ZASSERT(request != nullptr);
            ZEXPECT(request->id == RequestID("abc"));
        });
    }

    kit.add("parse_message_reads_a_notification", [] {
        Codec codec;
        auto params = A::encode(NoteParams{.text = "hello"});
        auto parsed = codec.parse_message(A::notification_raw("log/info", params));
        const auto* notification = std::get_if<ipc::IncomingNotification>(&parsed);
        ZASSERT(notification != nullptr);
        ZEXPECT(notification->method == "log/info");
        ZEXPECT(notification->params == params);
    });

    kit.add("parse_message_reads_a_result", [] {
        Codec codec;
        auto result = A::encode(AddResult{.sum = 42});
        auto parsed = codec.parse_message(A::response_raw(10, result));
        const auto* response = std::get_if<ipc::IncomingResponse>(&parsed);
        ZASSERT(response != nullptr);
        ZEXPECT(response->id == RequestID(10));
        ZEXPECT(response->result == result);
    });

    kit.add("parse_message_reads_an_error", [] {
        Codec codec;
        auto parsed = codec.parse_message(
            A::error_response(7, ipc::Error(ErrorCode::MethodNotFound, "method not found")));
        const auto* response = std::get_if<ipc::IncomingErrorResponse>(&parsed);
        ZASSERT(response != nullptr);
        ZEXPECT(response->id == RequestID(7));
        ZEXPECT(code_of(response->error) == ErrorCode::MethodNotFound);
        ZEXPECT(response->error.message == "method not found");
    });

    kit.add("parse_message_reads_an_error_without_an_id", [] {
        Codec codec;
        auto parsed = codec.parse_message(
            A::error_response(std::nullopt, ipc::Error(ErrorCode::ParseError, "bad")));
        const auto* response = std::get_if<ipc::IncomingErrorResponse>(&parsed);
        ZASSERT(response != nullptr);
        ZEXPECT(!response->id.has_value());
        ZEXPECT(code_of(response->error) == ErrorCode::ParseError);
    });

    kit.add("request_with_empty_params_roundtrip", [] {
        Codec codec;
        auto encoded = codec.encode_request(1, "test/empty", "");
        ZASSERT(encoded.has_value());
        auto parsed = codec.parse_message(*encoded);
        const auto* request = std::get_if<ipc::IncomingRequest>(&parsed);
        ZASSERT(request != nullptr);
        ZEXPECT(request->method == "test/empty");
        ZEXPECT(request->params.empty());
    });

    // Params and results of any size travel whole: past the lengths a
    // codec writes in one byte, and in two.
    kit.add("long_params_and_results_roundtrip", [] {
        Codec codec;
        for(std::size_t size: {300, 70'000}) {
            ZEST_CONTEXT("size {}", size);
            auto body = A::encode(NoteParams{.text = std::string(size, 'x')});
            auto request = codec.encode_request(1, "test/note", body);
            auto response = codec.encode_success_response(2, body);
            ZASSERT(request.has_value());
            ZASSERT(response.has_value());
            auto sent = A::read(*request);
            auto answered = A::read(*response);
            ZASSERT(sent.has_value());
            ZASSERT(answered.has_value());
            ZEXPECT(sent->body == body);
            ZEXPECT(answered->body == body);
            auto parsed = codec.parse_message(A::request_raw(3, "test/note", body));
            const auto* incoming = std::get_if<ipc::IncomingRequest>(&parsed);
            ZASSERT(incoming != nullptr);
            ZEXPECT(incoming->params == body);
        }
    });

    // A codec may read a message or a value in place, but leaves the text as
    // it was.
    kit.add("parse_message_leaves_the_payload_as_it_was", [] {
        Codec codec;
        auto payload = request<A>(5, "test/add", AddParams{.a = 1, .b = 2});
        const auto before = payload;
        auto parsed = codec.parse_message(payload);
        ZEXPECT(std::holds_alternative<ipc::IncomingRequest>(parsed));
        ZEXPECT(payload == before);
    });

    kit.add("deserialize_value_leaves_the_text_as_it_was", [] {
        Codec codec;
        auto text = A::encode(AddParams{.a = 1, .b = 2});
        const auto before = text;
        auto value = codec.template deserialize_value<AddParams>(text);
        ZASSERT(value.has_value());
        ZEXPECT(*value == AddParams{.a = 1, .b = 2});
        ZEXPECT(text == before);
        auto other = A::encode(NoteParams{.text = "x"});
        const auto other_before = other;
        ZEXPECT(!codec.template deserialize_value<AddParams>(other).has_value());
        ZEXPECT(other == other_before);
    });

    kit.add("parse_message_of_garbage_fails", [] {
        Codec codec;
        auto parsed = codec.parse_message(A::garbage);
        const auto* failure = std::get_if<ipc::IncomingParseError>(&parsed);
        ZASSERT(failure != nullptr);
        ZEXPECT(code_of(failure->error) == ErrorCode::ParseError);
        ZEXPECT(!failure->id.has_value());
    });

    kit.add("parse_message_of_nothing_fails", [] {
        Codec codec;
        auto parsed = codec.parse_message("");
        const auto* failure = std::get_if<ipc::IncomingParseError>(&parsed);
        ZASSERT(failure != nullptr);
        ZEXPECT(code_of(failure->error) == ErrorCode::ParseError);
    });

    // A message too large to read shows only its first bytes, cut here
    // inside its last member.
    auto cut = [](std::string message) {
        return message.substr(0, message.size() - 16);
    };
    auto long_note = [] {
        return NoteParams{.text = std::string(64, 'x')};
    };

    kit.add("peek_reads_a_request_head", [cut, long_note] {
        Codec codec;
        auto head = codec.peek(cut(request<A>(5, "test/note", long_note())));
        ZEXPECT(head.kind == ipc::MessageHead::Kind::Request);
        ZEXPECT(head.id == RequestID(5));
    });

    kit.add("peek_reads_a_notification_head", [cut, long_note] {
        Codec codec;
        auto head = codec.peek(cut(notification<A>("test/note", long_note())));
        ZEXPECT(head.kind == ipc::MessageHead::Kind::Notification);
        ZEXPECT(!head.id.has_value());
    });

    kit.add("peek_reads_a_response_head", [cut, long_note] {
        Codec codec;
        auto result = codec.peek(cut(response<A>(9, long_note())));
        ZEXPECT(result.kind == ipc::MessageHead::Kind::Response);
        ZEXPECT(result.id == RequestID(9));
        auto error = codec.peek(A::error_response(4, ipc::Error(ErrorCode::InternalError, "x")));
        ZEXPECT(error.kind == ipc::MessageHead::Kind::Response);
        ZEXPECT(error.id == RequestID(4));
    });

    kit.add("peek_of_bytes_that_start_no_message_knows_nothing", [] {
        Codec codec;
        auto head = codec.peek("??");
        ZEXPECT(head.kind == ipc::MessageHead::Kind::Unknown);
        ZEXPECT(!head.id.has_value());
    });

    kit.add("serialize_value_writes_what_the_codec_writes", [] {
        Codec codec;
        auto serialized = codec.serialize_value(AddParams{.a = 1, .b = 2});
        ZASSERT(serialized.has_value());
        ZEXPECT(*serialized == A::encode(AddParams{.a = 1, .b = 2}));
    });

    kit.add("serialize_value_of_an_unwritable_value_fails", [] {
        Codec codec;
        auto serialized = codec.serialize_value(Unwritable{});
        ZASSERT(!serialized.has_value());
        ZEXPECT(code_of(serialized.error()) == ErrorCode::InternalError);
        ZEXPECT(zest::contains(serialized.error().message, "unwritable"));
    });

    kit.add("deserialize_value_reads_what_the_codec_writes", [] {
        Codec codec;
        auto value =
            codec.template deserialize_value<AddParams>(A::encode(AddParams{.a = 4, .b = 5}));
        ZASSERT(value.has_value());
        ZEXPECT(*value == AddParams{.a = 4, .b = 5});
    });

    kit.add("deserialize_value_failure_carries_the_given_code", [] {
        Codec codec;
        auto by_default = codec.template deserialize_value<AddParams>(A::not_a_value);
        ZASSERT(!by_default.has_value());
        ZEXPECT(code_of(by_default.error()) == ErrorCode::RequestFailed);
        auto given =
            codec.template deserialize_value<AddParams>(A::not_a_value, ErrorCode::InvalidParams);
        ZASSERT(!given.has_value());
        ZEXPECT(code_of(given.error()) == ErrorCode::InvalidParams);
    });

    // What a params-less message carries reads as params without fields,
    // and only as those: nothing is not params with fields.
    kit.add("deserialize_value_of_nothing_reads_empty_params", [] {
        Codec codec;
        std::string nothing;
        ZEXPECT(codec.template deserialize_value<EmptyParams>("").has_value());
        ZEXPECT(codec.template deserialize_value<EmptyParams>(nothing).has_value());
    });

    kit.add("deserialize_value_of_nothing_into_fields_fails", [] {
        Codec codec;
        std::string nothing;
        auto from_view = codec.template deserialize_value<AddParams>("", ErrorCode::InvalidParams);
        auto in_place =
            codec.template deserialize_value<AddParams>(nothing, ErrorCode::InvalidParams);
        ZASSERT(!from_view.has_value());
        ZASSERT(!in_place.has_value());
        ZEXPECT(code_of(from_view.error()) == ErrorCode::InvalidParams);
        ZEXPECT(code_of(in_place.error()) == ErrorCode::InvalidParams);
    });
}

/// An error's data survives its codec.
template <CodecAdapter A>
void error_response_roundtrip_keeps_the_data() {
    typename A::Codec codec;
    codec::dyn::Value data{
        {"detail",  "bad state"},
        {"attempt", -1         },
    };
    auto encoded = codec.encode_error_response(
        3,
        ipc::Error(ipc::protocol::ErrorCode::InvalidParams, "rejected", data));
    ZASSERT(encoded.has_value());
    auto parsed = codec.parse_message(*encoded);
    const auto* response = std::get_if<ipc::IncomingErrorResponse>(&parsed);
    ZASSERT(response != nullptr);
    ZASSERT(response->error.data.has_value());
    ZEXPECT(*response->error.data == data);
}

}  // namespace kota::test
