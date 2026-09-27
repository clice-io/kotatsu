#pragma once

// The shared suite of the ipc codecs. JsonCodec and BincodeCodec implement one
// duck-typed protocol (parse_message, encode_*, serialize_value,
// deserialize_value), so its behaviour is written once, below, and each codec
// runs it through an adapter over its wire format (codec_json.h,
// codec_bincode.h):
//
//     ZEST_CASE_GROUP(protocol) {
//         test::codec_protocol(test::CodecKit<test::JsonWire>{add_case});
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
template <typename W>
concept Wire = requires(const RequestID& id,
                        const std::optional<RequestID>& maybe_id,
                        std::string_view text,
                        const ipc::Error& error) {
    typename W::Codec;
    { W::name } -> std::convertible_to<std::string_view>;
    { W::caps } -> std::convertible_to<Caps>;
    /// A payload that is no message.
    { W::garbage } -> std::convertible_to<std::string_view>;
    /// A body no struct with fields decodes from.
    { W::not_a_value } -> std::convertible_to<std::string_view>;
    { W::encode(AddParams{}) } -> std::same_as<std::string>;
    { W::template decode<AddParams>(text) } -> std::same_as<std::optional<AddParams>>;
    { W::request_raw(id, text, text) } -> std::same_as<std::string>;
    { W::notification_raw(text, text) } -> std::same_as<std::string>;
    { W::response_raw(id, text) } -> std::same_as<std::string>;
    { W::error_response(maybe_id, error) } -> std::same_as<std::string>;
    { W::read(text) } -> std::same_as<std::expected<Message, std::string>>;
};

/// A request for `method` with `params`, as the remote sends it.
template <Wire W, typename T>
std::string request(const RequestID& id, std::string_view method, const T& params) {
    return W::request_raw(id, method, W::encode(params));
}

/// A notification for `method` with `params`, as the remote sends it.
template <Wire W, typename T>
std::string notification(std::string_view method, const T& params) {
    return W::notification_raw(method, W::encode(params));
}

/// A success response carrying `result`, as the remote sends it.
template <Wire W, typename T>
std::string response(const RequestID& id, const T& result) {
    return W::response_raw(id, W::encode(result));
}

/// `body`, a message's params or result, decoded as T.
template <typename T, Wire W>
std::optional<T> decoded(std::string_view body) {
    return W::template decode<T>(body);
}

/// The code of `error`, as the enumerator it matches.
inline ipc::protocol::ErrorCode code_of(const ipc::Error& error) {
    return static_cast<ipc::protocol::ErrorCode>(error.code);
}

/// Where the codec cases of adapter W are registered.
template <Wire W>
struct CodecKit {
    const zest::CaseRegistrar& add;
};

template <Wire W>
void codec_protocol(const CodecKit<W>& kit) {
    using ipc::protocol::ErrorCode;
    using Codec = typename W::Codec;

    kit.add("encode_request_writes_a_request", [] {
        Codec codec;
        auto encoded = codec.encode_request(7, "test/add", W::encode(AddParams{.a = 1, .b = 2}));
        ASSERT(encoded.has_value());
        auto message = W::read(*encoded);
        ASSERT(message.has_value());
        EXPECT(message->kind == Message::Kind::Request);
        EXPECT(message->id == RequestID(7));
        EXPECT(message->method == "test/add");
        auto params = decoded<AddParams, W>(message->body);
        ASSERT(params.has_value());
        EXPECT(*params == AddParams{.a = 1, .b = 2});
    });

    if constexpr(W::caps.string_ids) {
        kit.add("encode_request_writes_a_string_id", [] {
            Codec codec;
            auto encoded = codec.encode_request("abc", "test/add", W::encode(AddParams{}));
            ASSERT(encoded.has_value());
            auto message = W::read(*encoded);
            ASSERT(message.has_value());
            EXPECT(message->id == RequestID("abc"));
        });
    } else {
        kit.add("encode_request_with_a_string_id_fails", [] {
            Codec codec;
            auto encoded = codec.encode_request("abc", "test/add", W::encode(AddParams{}));
            ASSERT(!encoded.has_value());
            EXPECT(code_of(encoded.error()) == ErrorCode::InternalError);
        });
    }

    kit.add("encode_notification_writes_a_notification", [] {
        Codec codec;
        auto encoded = codec.encode_notification("test/note", W::encode(NoteParams{.text = "hi"}));
        ASSERT(encoded.has_value());
        auto message = W::read(*encoded);
        ASSERT(message.has_value());
        EXPECT(message->kind == Message::Kind::Notification);
        EXPECT(!message->id.has_value());
        EXPECT(message->method == "test/note");
        auto params = decoded<NoteParams, W>(message->body);
        ASSERT(params.has_value());
        EXPECT(params->text == "hi");
    });

    kit.add("encode_success_response_writes_a_result", [] {
        Codec codec;
        auto encoded = codec.encode_success_response(42, W::encode(AddResult{.sum = 3}));
        ASSERT(encoded.has_value());
        auto message = W::read(*encoded);
        ASSERT(message.has_value());
        EXPECT(message->kind == Message::Kind::Result);
        EXPECT(message->id == RequestID(42));
        auto result = decoded<AddResult, W>(message->body);
        ASSERT(result.has_value());
        EXPECT(result->sum == 3);
    });

    kit.add("encode_error_response_writes_an_error", [] {
        Codec codec;
        auto encoded =
            codec.encode_error_response(20,
                                        ipc::Error(ErrorCode::InternalError, "something broke"));
        ASSERT(encoded.has_value());
        auto message = W::read(*encoded);
        ASSERT(message.has_value());
        EXPECT(message->kind == Message::Kind::Error);
        EXPECT(message->id == RequestID(20));
        EXPECT(code_of(message->error) == ErrorCode::InternalError);
        EXPECT(message->error.message == "something broke");
    });

    kit.add("parse_message_reads_a_request", [] {
        Codec codec;
        auto params = W::encode(AddParams{.a = 1, .b = 2});
        auto parsed = codec.parse_message(W::request_raw(99, "math/add", params));
        const auto* request = std::get_if<ipc::IncomingRequest>(&parsed);
        ASSERT(request != nullptr);
        EXPECT(request->id == RequestID(99));
        EXPECT(request->method == "math/add");
        EXPECT(request->params == params);
    });

    if constexpr(W::caps.string_ids) {
        kit.add("parse_message_reads_a_string_id", [] {
            Codec codec;
            auto parsed = codec.parse_message(request<W>("abc", "test/add", AddParams{}));
            const auto* request = std::get_if<ipc::IncomingRequest>(&parsed);
            ASSERT(request != nullptr);
            EXPECT(request->id == RequestID("abc"));
        });
    }

    kit.add("parse_message_reads_a_notification", [] {
        Codec codec;
        auto params = W::encode(NoteParams{.text = "hello"});
        auto parsed = codec.parse_message(W::notification_raw("log/info", params));
        const auto* notification = std::get_if<ipc::IncomingNotification>(&parsed);
        ASSERT(notification != nullptr);
        EXPECT(notification->method == "log/info");
        EXPECT(notification->params == params);
    });

    kit.add("parse_message_reads_a_result", [] {
        Codec codec;
        auto result = W::encode(AddResult{.sum = 42});
        auto parsed = codec.parse_message(W::response_raw(10, result));
        const auto* response = std::get_if<ipc::IncomingResponse>(&parsed);
        ASSERT(response != nullptr);
        EXPECT(response->id == RequestID(10));
        EXPECT(response->result == result);
    });

    kit.add("parse_message_reads_an_error", [] {
        Codec codec;
        auto parsed = codec.parse_message(
            W::error_response(7, ipc::Error(ErrorCode::MethodNotFound, "method not found")));
        const auto* response = std::get_if<ipc::IncomingErrorResponse>(&parsed);
        ASSERT(response != nullptr);
        EXPECT(response->id == RequestID(7));
        EXPECT(code_of(response->error) == ErrorCode::MethodNotFound);
        EXPECT(response->error.message == "method not found");
    });

    kit.add("request_with_empty_params_roundtrip", [] {
        Codec codec;
        auto encoded = codec.encode_request(1, "test/empty", "");
        ASSERT(encoded.has_value());
        auto parsed = codec.parse_message(*encoded);
        const auto* request = std::get_if<ipc::IncomingRequest>(&parsed);
        ASSERT(request != nullptr);
        EXPECT(request->method == "test/empty");
        EXPECT(request->params.empty());
    });

    kit.add("parse_message_of_garbage_fails", [] {
        Codec codec;
        auto parsed = codec.parse_message(W::garbage);
        const auto* failure = std::get_if<ipc::IncomingParseError>(&parsed);
        ASSERT(failure != nullptr);
        EXPECT(code_of(failure->error) == ErrorCode::ParseError);
    });

    kit.add("parse_message_of_nothing_fails", [] {
        Codec codec;
        auto parsed = codec.parse_message("");
        const auto* failure = std::get_if<ipc::IncomingParseError>(&parsed);
        ASSERT(failure != nullptr);
        EXPECT(code_of(failure->error) == ErrorCode::ParseError);
    });

    kit.add("serialize_value_writes_what_the_codec_writes", [] {
        Codec codec;
        auto serialized = codec.serialize_value(AddParams{.a = 1, .b = 2});
        ASSERT(serialized.has_value());
        EXPECT(*serialized == W::encode(AddParams{.a = 1, .b = 2}));
    });

    kit.add("serialize_value_of_an_unwritable_value_fails", [] {
        Codec codec;
        auto serialized = codec.serialize_value(Unwritable{});
        ASSERT(!serialized.has_value());
        EXPECT(code_of(serialized.error()) == ErrorCode::InternalError);
        EXPECT(zest::contains(serialized.error().message, "unwritable"));
    });

    kit.add("deserialize_value_reads_what_the_codec_writes", [] {
        Codec codec;
        auto value =
            codec.template deserialize_value<AddParams>(W::encode(AddParams{.a = 4, .b = 5}));
        ASSERT(value.has_value());
        EXPECT(*value == AddParams{.a = 4, .b = 5});
    });

    kit.add("deserialize_value_failure_carries_the_given_code", [] {
        Codec codec;
        auto by_default = codec.template deserialize_value<AddParams>(W::not_a_value);
        ASSERT(!by_default.has_value());
        EXPECT(code_of(by_default.error()) == ErrorCode::RequestFailed);
        auto given =
            codec.template deserialize_value<AddParams>(W::not_a_value, ErrorCode::InvalidParams);
        ASSERT(!given.has_value());
        EXPECT(code_of(given.error()) == ErrorCode::InvalidParams);
    });

    // What a params-less message carries reads as params without fields.
    kit.add("deserialize_value_of_nothing_reads_empty_params", [] {
        Codec codec;
        EXPECT(codec.template deserialize_value<EmptyParams>("").has_value());
    });
}

/// Nothing is not params with fields: reading it fails rather than making up
/// a default value.
template <Wire W>
void deserialize_value_of_nothing_into_fields_fails() {
    typename W::Codec codec;
    auto value =
        codec.template deserialize_value<AddParams>("", ipc::protocol::ErrorCode::InvalidParams);
    ASSERT(!value.has_value());
    EXPECT(code_of(value.error()) == ipc::protocol::ErrorCode::InvalidParams);
}

/// An error's data survives its codec.
template <Wire W>
void error_response_roundtrip_keeps_the_data() {
    typename W::Codec codec;
    codec::dyn::Value data{
        {"detail",  "bad state"},
        {"attempt", -1         },
    };
    auto encoded = codec.encode_error_response(
        3,
        ipc::Error(ipc::protocol::ErrorCode::InvalidParams, "rejected", data));
    ASSERT(encoded.has_value());
    auto parsed = codec.parse_message(*encoded);
    const auto* response = std::get_if<ipc::IncomingErrorResponse>(&parsed);
    ASSERT(response != nullptr);
    ASSERT(response->error.data.has_value());
    EXPECT(*response->error.data == data);
}

}  // namespace kota::test
