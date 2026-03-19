#pragma once

#include <type_traits>

#include "eventide/ipc/codec.h"
#include "eventide/serde/json/json.h"
#include "eventide/serde/serde/config.h"
#include "eventide/serde/serde/raw_value.h"
#include "eventide/serde/serde/spelling.h"

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
