#include "eventide/ipc/bincode_codec.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include "eventide/serde/raw_value.h"

namespace eventide::ipc {

namespace {

// --- Bincode envelope variant ---

struct bincode_request {
    protocol::RequestID id;
    std::string method;
    serde::RawValue params;
};

struct bincode_notification {
    std::string method;
    serde::RawValue params;
};

struct bincode_success {
    protocol::RequestID id;
    serde::RawValue result;
};

struct bincode_error {
    std::optional<protocol::RequestID> id;
    std::int32_t code = 0;
    std::string message;
    serde::RawValue data;
};

using bincode_envelope =
    std::variant<bincode_request, bincode_notification, bincode_success, bincode_error>;

struct cancel_request_params {
    protocol::RequestID id;
};

}  // namespace

IncomingMessage BincodeCodec::parse_message(std::string_view payload) {
    IncomingMessage msg;

    auto bytes_span = std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload.data()),
                                                 payload.size());

    bincode_envelope envelope;
    auto status = serde::bincode::from_bytes(bytes_span, envelope);
    if(!status) {
        msg.parse_error = RPCError(protocol::ErrorCode::ParseError,
                                   std::string(serde::bincode::error_message(status.error())));
        return msg;
    }

    std::visit(
        [&](auto&& v) {
            using T = std::remove_cvref_t<decltype(v)>;
            if constexpr(std::is_same_v<T, bincode_request>) {
                msg.id = v.id;
                msg.method = std::move(v.method);
                msg.params = std::move(v.params.data);
            } else if constexpr(std::is_same_v<T, bincode_notification>) {
                msg.method = std::move(v.method);
                msg.params = std::move(v.params.data);
            } else if constexpr(std::is_same_v<T, bincode_success>) {
                msg.id = v.id;
                msg.result = std::move(v.result.data);
            } else if constexpr(std::is_same_v<T, bincode_error>) {
                if(v.id.has_value()) {
                    msg.id = *v.id;
                }
                msg.error = RPCError(static_cast<protocol::integer>(v.code), std::move(v.message));
            }
        },
        std::move(envelope));

    return msg;
}

Result<std::string> BincodeCodec::encode_request(const protocol::RequestID& id,
                                                 std::string_view method,
                                                 std::string_view params) {
    bincode_envelope envelope =
        bincode_request{id, std::string(method), serde::RawValue{std::string(params)}};
    auto bytes = serde::bincode::to_bytes(envelope);
    if(!bytes) {
        return std::unexpected(RPCError(protocol::ErrorCode::InternalError,
                                        std::string(serde::bincode::error_message(bytes.error()))));
    }
    return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

Result<std::string> BincodeCodec::encode_notification(std::string_view method,
                                                      std::string_view params) {
    bincode_envelope envelope =
        bincode_notification{std::string(method), serde::RawValue{std::string(params)}};
    auto bytes = serde::bincode::to_bytes(envelope);
    if(!bytes) {
        return std::unexpected(RPCError(protocol::ErrorCode::InternalError,
                                        std::string(serde::bincode::error_message(bytes.error()))));
    }
    return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

Result<std::string> BincodeCodec::encode_success_response(const protocol::RequestID& id,
                                                          std::string_view result) {
    bincode_envelope envelope = bincode_success{id, serde::RawValue{std::string(result)}};
    auto bytes = serde::bincode::to_bytes(envelope);
    if(!bytes) {
        return std::unexpected(RPCError(protocol::ErrorCode::InternalError,
                                        std::string(serde::bincode::error_message(bytes.error()))));
    }
    return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

Result<std::string> BincodeCodec::encode_error_response(const protocol::RequestID& id,
                                                        const RPCError& error) {
    std::optional<protocol::RequestID> wire_id;
    if(id.has_value()) {
        wire_id = id;
    }
    bincode_envelope envelope = bincode_error{
        wire_id,
        static_cast<std::int32_t>(error.code),
        error.message,
        serde::RawValue{},
    };
    auto bytes = serde::bincode::to_bytes(envelope);
    if(!bytes) {
        return std::unexpected(RPCError(protocol::ErrorCode::InternalError,
                                        std::string(serde::bincode::error_message(bytes.error()))));
    }
    return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

std::optional<protocol::RequestID> BincodeCodec::parse_cancel_id(std::string_view params) {
    auto bytes_span = std::span<const std::byte>(reinterpret_cast<const std::byte*>(params.data()),
                                                 params.size());
    cancel_request_params parsed;
    auto status = serde::bincode::from_bytes(bytes_span, parsed);
    if(!status) {
        return std::nullopt;
    }
    return parsed.id;
}

}  // namespace eventide::ipc
