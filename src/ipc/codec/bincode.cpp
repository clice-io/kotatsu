#include "kota/ipc/codec/bincode.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace kota::ipc {

namespace {

// The messages as they are read: each kind's fields up to its params or
// result, which take the rest of the payload, or, for an error, all of them.

struct request_envelope {
    protocol::RequestID id;
    std::string method;
};

struct notification_envelope {
    std::string method;
};

struct success_envelope {
    protocol::RequestID id;
};

struct error_envelope {
    std::optional<protocol::RequestID> id;
    std::int32_t code = 0;
    std::string message;
    std::optional<codec::dyn::Value> data;
};

using envelope =
    std::variant<request_envelope, notification_envelope, success_envelope, error_envelope>;

// The envelopes' first fields, as far as they tell a message's kind and id.

struct request_head {
    protocol::RequestID id;
};

struct notification_head {};

struct success_head {
    protocol::RequestID id;
};

struct error_head {
    std::optional<protocol::RequestID> id;
};

using envelope_head = std::variant<request_head, notification_head, success_head, error_head>;

}  // namespace

Error BincodeCodec::codec_error(protocol::ErrorCode code, const codec::rich_error& error) {
    return Error(code, error.to_string());
}

IncomingMessage BincodeCodec::parse_message(std::string payload) {
    codec::rich_error error;
    codec::scoped_context<codec::rich_error> guard(error);
    codec::bincode::Reader reader{
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload.data()),
                                   payload.size())};
    envelope read;
    if(!codec::decode_value<codec::default_config<>>(reader, read)) {
        return IncomingParseError{
            .id = std::nullopt,
            .error = codec_error(protocol::ErrorCode::ParseError, error),
        };
    }

    // The params or result: the rest of the payload.
    const auto offset = reader.pos;
    auto rest = [&] {
        const auto size = payload.size() - offset;
        return PayloadSlice{.payload = std::move(payload), .offset = offset, .size = size};
    };
    return std::visit(
        [&](auto& fields) -> IncomingMessage {
            using T = std::remove_cvref_t<decltype(fields)>;
            if constexpr(std::is_same_v<T, request_envelope>) {
                return IncomingRequest{
                    .id = std::move(fields.id),
                    .method = std::move(fields.method),
                    .params = rest(),
                };
            } else if constexpr(std::is_same_v<T, notification_envelope>) {
                return IncomingNotification{.method = std::move(fields.method), .params = rest()};
            } else if constexpr(std::is_same_v<T, success_envelope>) {
                return IncomingResponse{.id = std::move(fields.id), .result = rest()};
            } else {
                if(offset != payload.size()) {
                    return IncomingParseError{
                        .id = std::nullopt,
                        .error = Error(protocol::ErrorCode::ParseError, "trailing bytes"),
                    };
                }
                return IncomingErrorResponse{
                    .id = std::move(fields.id),
                    .error = Error(static_cast<protocol::integer>(fields.code),
                                   std::move(fields.message),
                                   std::move(fields.data)),
                };
            }
        },
        read);
}

MessageHead BincodeCodec::peek(std::string_view prefix) {
    codec::rich_error error;
    codec::scoped_context<codec::rich_error> guard(error);
    codec::bincode::Reader reader{
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(prefix.data()),
                                   prefix.size())};
    envelope_head head;
    if(!codec::decode_value<codec::default_config<>>(reader, head)) {
        return {};
    }
    return std::visit(
        [](auto& fields) -> MessageHead {
            using T = std::remove_cvref_t<decltype(fields)>;
            if constexpr(std::is_same_v<T, request_head>) {
                return {.kind = MessageHead::Kind::Request, .id = std::move(fields.id)};
            } else if constexpr(std::is_same_v<T, notification_head>) {
                return {.kind = MessageHead::Kind::Notification};
            } else {
                return {.kind = MessageHead::Kind::Response, .id = std::move(fields.id)};
            }
        },
        head);
}

Result<std::string>
    BincodeCodec::encode_error_response(const std::optional<protocol::RequestID>& id,
                                        const Error& error) {
    return serialize_value(envelope(error_envelope{
        .id = id,
        .code = static_cast<std::int32_t>(error.code),
        .message = error.message,
        .data = error.data,
    }));
}

template class Peer<BincodeCodec>;

}  // namespace kota::ipc
