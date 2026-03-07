#pragma once

#include <concepts>
#include <span>
#include <string>

#include "eventide/ipc/codec.h"
#include "eventide/serde/bincode/bincode.h"
#include "eventide/serde/raw_value.h"

namespace eventide::serde {

// Bincode serialization: write int64 directly
template <>
struct serialize_traits<bincode::Serializer, eventide::ipc::protocol::RequestID> {
    using value_type = typename bincode::Serializer::value_type;
    using error_type = typename bincode::Serializer::error_type;

    static auto serialize(bincode::Serializer& serializer,
                          const eventide::ipc::protocol::RequestID& id)
        -> std::expected<value_type, error_type> {
        return serde::serialize(serializer, id.value);
    }
};

// Bincode deserialization: read int64 directly, set state to integer
template <>
struct deserialize_traits<bincode::Deserializer, eventide::ipc::protocol::RequestID> {
    using error_type = typename bincode::Deserializer::error_type;

    static auto deserialize(bincode::Deserializer& deserializer,
                            eventide::ipc::protocol::RequestID& id)
        -> std::expected<void, error_type> {
        auto status = serde::deserialize(deserializer, id.value);
        if(!status) {
            return std::unexpected(status.error());
        }
        id.state = eventide::ipc::protocol::RequestID::State::integer;
        return {};
    }
};

}  // namespace eventide::serde

namespace eventide::ipc {

class BincodeCodec {
public:
    IncomingMessage parse_message(std::string_view payload);

    Result<std::string> encode_request(const protocol::RequestID& id,
                                       std::string_view method,
                                       std::string_view params);

    Result<std::string> encode_notification(std::string_view method, std::string_view params);

    Result<std::string> encode_success_response(const protocol::RequestID& id,
                                                std::string_view result);

    Result<std::string> encode_error_response(const protocol::RequestID& id, const RPCError& error);

    std::optional<protocol::RequestID> parse_cancel_id(std::string_view params);

    template <typename T>
    Result<std::string> serialize_value(const T& value) {
        auto bytes = serde::bincode::to_bytes(value);
        if(!bytes) {
            return std::unexpected(
                RPCError(protocol::ErrorCode::InternalError,
                         std::string(serde::bincode::error_message(bytes.error()))));
        }
        return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    }

    template <typename T>
    Result<T> deserialize_value(std::string_view raw,
                                protocol::ErrorCode code = protocol::ErrorCode::RequestFailed) {
        if(raw.empty()) {
            if constexpr(std::default_initializable<T>) {
                return T{};
            } else {
                return std::unexpected(RPCError(code, "empty params"));
            }
        }
        auto bytes_span =
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(raw.data()), raw.size());
        T value{};
        auto status = serde::bincode::from_bytes(bytes_span, value);
        if(!status) {
            return std::unexpected(
                RPCError(code, std::string(serde::bincode::error_message(status.error()))));
        }
        return value;
    }
};

}  // namespace eventide::ipc
