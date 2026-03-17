#pragma once

#include <type_traits>

#include "eventide/ipc/codec.h"
#include "eventide/serde/json/json.h"
#include "eventide/serde/serde/config.h"
#include "eventide/serde/serde/raw_value.h"
#include "eventide/serde/serde/spelling.h"

namespace eventide::serde {

// JSON serialization: integer → int, absent/null → null
template <typename Config>
struct serialize_traits<json::Serializer<Config>, eventide::ipc::protocol::RequestID> {
    using value_type = typename json::Serializer<Config>::value_type;
    using error_type = typename json::Serializer<Config>::error_type;

    static auto serialize(json::Serializer<Config>& serializer,
                          const eventide::ipc::protocol::RequestID& id)
        -> std::expected<value_type, error_type> {
        if(id.has_value()) {
            return serializer.serialize_int(id.value);
        }
        return serializer.serialize_null();
    }
};

// JSON deserialization: three-state (absent kept by struct skip, null/integer deserialized)
// Uses peek_type to dispatch: null → null state, number → integer state, else → error.
template <typename Config>
struct deserialize_traits<json::Deserializer<Config>, eventide::ipc::protocol::RequestID> {
    using error_type = typename json::Deserializer<Config>::error_type;

    static auto deserialize(json::Deserializer<Config>& deserializer,
                            eventide::ipc::protocol::RequestID& id)
        -> std::expected<void, error_type> {
        auto type = deserializer.peek_type();
        if(!type) {
            return std::unexpected(type.error());
        }

        if(*type == simdjson::ondemand::json_type::null) {
            auto is_null = deserializer.deserialize_none();
            if(!is_null) {
                return std::unexpected(is_null.error());
            }
            id = eventide::ipc::protocol::RequestID::null_id();
            return {};
        }

        if(*type == simdjson::ondemand::json_type::number) {
            std::int64_t v = 0;
            auto status = deserializer.deserialize_int(v);
            if(!status) {
                return std::unexpected(status.error());
            }
            id = eventide::ipc::protocol::RequestID(v);
            return {};
        }

        return std::unexpected(error_type::type_mismatch);
    }
};

}  // namespace eventide::serde

namespace eventide::ipc {

struct lsp_config {
    using field_rename = serde::rename_policy::lower_camel;
};

class JsonCodec {
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
        auto serialized = serde::json::to_string<lsp_config>(value);
        if(!serialized) {
            return outcome_error(
                Error(protocol::ErrorCode::InternalError,
                      std::string(serde::json::error_message(serialized.error()))));
        }
        return std::move(*serialized);
    }

    template <typename T>
    Result<T> deserialize_value(std::string_view raw,
                                protocol::ErrorCode code = protocol::ErrorCode::RequestFailed) {
        if(raw.empty()) {
            if constexpr(std::is_same_v<T, protocol::null> || std::is_same_v<T, protocol::Value>) {
                raw = "null";
            } else {
                raw = "{}";
            }
        }
        auto parsed = serde::json::parse<T, lsp_config>(raw);
        if(!parsed) {
            return outcome_error(
                Error(code, std::string(serde::json::error_message(parsed.error()))));
        }
        return std::move(*parsed);
    }
};

}  // namespace eventide::ipc
