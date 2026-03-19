#pragma once

#include <concepts>
#include <span>
#include <string>

#include "eventide/ipc/codec.h"
#include "eventide/serde/bincode/bincode.h"
#include "eventide/serde/serde/raw_value.h"

namespace eventide::serde {

// Bincode serialization: write int64 directly (bincode only uses integer IDs)
template <typename Config>
struct serialize_traits<bincode::Serializer<Config>, eventide::ipc::protocol::RequestID> {
    using value_type = typename bincode::Serializer<Config>::value_type;
    using error_type = typename bincode::Serializer<Config>::error_type;

    static auto serialize(bincode::Serializer<Config>& serializer,
                          const eventide::ipc::protocol::RequestID& id)
        -> std::expected<value_type, error_type> {
        auto* int_id = std::get_if<std::int64_t>(&id);
        if(!int_id) {
            return std::unexpected(error_type::type_mismatch);
        }
        return serde::serialize(serializer, *int_id);
    }
};

// Bincode deserialization: read int64 directly
template <typename Config>
struct deserialize_traits<bincode::Deserializer<Config>, eventide::ipc::protocol::RequestID> {
    using error_type = typename bincode::Deserializer<Config>::error_type;

    static auto deserialize(bincode::Deserializer<Config>& deserializer,
                            eventide::ipc::protocol::RequestID& id)
        -> std::expected<void, error_type> {
        std::int64_t v = 0;
        auto status = serde::deserialize(deserializer, v);
        if(!status) {
            return std::unexpected(status.error());
        }
        id.emplace<std::int64_t>(v);
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

    Result<std::string> encode_error_response(const protocol::RequestID& id, const Error& error);

    template <typename T>
    Result<std::string> serialize_value(const T& value) {
        auto bytes = serde::bincode::to_bytes(value);
        if(!bytes) {
            return outcome_error(Error(protocol::ErrorCode::InternalError,
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
                return outcome_error(Error(code, "empty params"));
            }
        }
        auto bytes_span =
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(raw.data()), raw.size());
        T value{};
        auto status = serde::bincode::from_bytes(bytes_span, value);
        if(!status) {
            return outcome_error(
                Error(code, std::string(serde::bincode::error_message(status.error()))));
        }
        return value;
    }
};

}  // namespace eventide::ipc
