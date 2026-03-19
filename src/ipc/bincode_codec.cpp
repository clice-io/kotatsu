#include "eventide/ipc/bincode_codec.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

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

Result<std::string> encode_envelope(const bincode_envelope& envelope) {
    auto bytes = serde::bincode::to_bytes(envelope);
    if(!bytes) {
        return outcome_error(Error(protocol::ErrorCode::InternalError,
                                   std::string(serde::bincode::error_message(bytes.error()))));
    }
    return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

}  // namespace

IncomingMessage BincodeCodec::parse_message(std::string_view payload) {
    auto bytes_span = std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload.data()),
                                                 payload.size());

    bincode_envelope envelope;
    auto status = serde::bincode::from_bytes(bytes_span, envelope);
    if(!status) {
        return IncomingParseError{
            Error(protocol::ErrorCode::ParseError,
                  std::string(serde::bincode::error_message(status.error())))};
    }

    return std::visit(
        [](auto&& v) -> IncomingMessage {
            using T = std::remove_cvref_t<decltype(v)>;
            if constexpr(std::is_same_v<T, bincode_request>) {
                return IncomingRequest{v.id, std::move(v.method), std::move(v.params.data)};
            } else if constexpr(std::is_same_v<T, bincode_notification>) {
                return IncomingNotification{std::move(v.method), std::move(v.params.data)};
            } else if constexpr(std::is_same_v<T, bincode_success>) {
                return IncomingResponse{v.id, std::move(v.result.data)};
            } else if constexpr(std::is_same_v<T, bincode_error>) {
                auto id = v.id.has_value() ? *v.id : protocol::RequestID{};
                return IncomingErrorResponse{
                    id,
                    Error(static_cast<protocol::integer>(v.code), std::move(v.message))};
            }
        },
        std::move(envelope));
}

Result<std::string> BincodeCodec::encode_request(const protocol::RequestID& id,
                                                 std::string_view method,
                                                 std::string_view params) {
    return encode_envelope(
        bincode_request{id, std::string(method), serde::RawValue{std::string(params)}});
}

Result<std::string> BincodeCodec::encode_notification(std::string_view method,
                                                      std::string_view params) {
    return encode_envelope(
        bincode_notification{std::string(method), serde::RawValue{std::string(params)}});
}

Result<std::string> BincodeCodec::encode_success_response(const protocol::RequestID& id,
                                                          std::string_view result) {
    return encode_envelope(bincode_success{id, serde::RawValue{std::string(result)}});
}

Result<std::string> BincodeCodec::encode_error_response(const protocol::RequestID& id,
                                                        const Error& error) {
    std::optional<protocol::RequestID> wire_id = id;
    return encode_envelope(bincode_error{
        wire_id,
        static_cast<std::int32_t>(error.code),
        error.message,
        serde::RawValue{},
    });
}

}  // namespace eventide::ipc
