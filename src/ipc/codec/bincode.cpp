#include "kota/ipc/codec/bincode.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace kota::ipc {

namespace {

struct bincode_request {
    protocol::RequestID id;
    std::string method;
    codec::RawValue params;
};

struct bincode_notification {
    std::string method;
    codec::RawValue params;
};

struct bincode_success {
    protocol::RequestID id;
    codec::RawValue result;
};

struct bincode_error {
    std::optional<protocol::RequestID> id;
    std::int32_t code = 0;
    std::string message;
    std::optional<codec::dyn::Value> data;
};

using bincode_envelope =
    std::variant<bincode_request, bincode_notification, bincode_success, bincode_error>;

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

// The envelopes as far as the params or result they end with, which a
// message appends to them as it is; in bincode_envelope's order, whose
// alternative index they write.

struct request_prefix {
    protocol::RequestID id;
    std::string_view method;
};

struct notification_prefix {
    std::string_view method;
};

struct success_prefix {
    protocol::RequestID id;
};

using envelope_prefix = std::variant<request_prefix, notification_prefix, success_prefix>;

/// A message: prefix, then raw, the params or result, copied once into a
/// message sized for it.
std::expected<std::string, codec::rich_error> message(const envelope_prefix& prefix,
                                                      std::string_view raw) {
    codec::rich_error error;
    codec::scoped_context<codec::rich_error> guard(error);
    std::vector<std::byte> head;
    codec::bincode::Writer writer{head};
    if(!codec::encode_value<codec::default_config<>>(writer, prefix)) {
        return std::unexpected(std::move(error));
    }
    writer.write_length(raw.size());
    std::string text;
    text.reserve(writer.size + raw.size());
    text.append(reinterpret_cast<const char*>(head.data()), writer.size);
    text.append(raw);
    return text;
}

}  // namespace

Error BincodeCodec::codec_error(protocol::ErrorCode code, const codec::rich_error& error) {
    return Error(code, error.to_string());
}

Result<std::string> BincodeCodec::encoded(std::expected<std::string, codec::rich_error> text) {
    if(!text) {
        return outcome_error(codec_error(protocol::ErrorCode::InternalError, text.error()));
    }
    return std::move(*text);
}

IncomingMessage BincodeCodec::parse_message(std::string_view payload) {
    auto bytes_span = std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload.data()),
                                                 payload.size());

    bincode_envelope envelope;
    auto status = codec::bincode::from_bytes(bytes_span, envelope);
    if(!status) {
        return IncomingParseError{
            .id = std::nullopt,
            .error = codec_error(protocol::ErrorCode::ParseError, status.error()),
        };
    }

    return std::visit(
        [](auto&& v) -> IncomingMessage {
            using T = std::remove_cvref_t<decltype(v)>;
            if constexpr(std::is_same_v<T, bincode_request>) {
                return IncomingRequest{
                    .id = v.id,
                    .method = std::move(v.method),
                    .params = std::move(v.params.data),
                };
            } else if constexpr(std::is_same_v<T, bincode_notification>) {
                return IncomingNotification{
                    .method = std::move(v.method),
                    .params = std::move(v.params.data),
                };
            } else if constexpr(std::is_same_v<T, bincode_success>) {
                return IncomingResponse{.id = v.id, .result = std::move(v.result.data)};
            } else if constexpr(std::is_same_v<T, bincode_error>) {
                return IncomingErrorResponse{
                    .id = std::move(v.id),
                    .error = Error(static_cast<protocol::integer>(v.code),
                                   std::move(v.message),
                                   std::move(v.data)),
                };
            }
        },
        std::move(envelope));
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

Result<std::string> BincodeCodec::encode_request(const protocol::RequestID& id,
                                                 std::string_view method,
                                                 std::string_view params) {
    return encoded(message(request_prefix{.id = id, .method = method}, params));
}

Result<std::string> BincodeCodec::encode_notification(std::string_view method,
                                                      std::string_view params) {
    return encoded(message(notification_prefix{.method = method}, params));
}

Result<std::string> BincodeCodec::encode_success_response(const protocol::RequestID& id,
                                                          std::string_view result) {
    return encoded(message(success_prefix{.id = id}, result));
}

Result<std::string>
    BincodeCodec::encode_error_response(const std::optional<protocol::RequestID>& id,
                                        const Error& error) {
    return serialize_value(bincode_envelope(bincode_error{
        .id = id,
        .code = static_cast<std::int32_t>(error.code),
        .message = error.message,
        .data = error.data,
    }));
}

template class Peer<BincodeCodec>;

}  // namespace kota::ipc
