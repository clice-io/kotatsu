#pragma once

#include <type_traits>

#include "kota/ipc/codec.h"
#include "kota/ipc/peer.h"
#include "kota/support/spelling.h"
#include "kota/codec/json/json.h"

namespace kota::ipc {

struct JsonCodec {
    IncomingMessage parse_message(std::string_view payload);

    /// Reads what it can from `prefix`, the first bytes of a message too
    /// large to read whole.
    MessageHead peek(std::string_view prefix);

    Result<std::string> encode_request(const protocol::RequestID& id,
                                       std::string_view method,
                                       std::string_view params);

    Result<std::string> encode_notification(std::string_view method, std::string_view params);

    Result<std::string> encode_success_response(const protocol::RequestID& id,
                                                std::string_view result);

    /// Without an id, the error answers a message whose id could not be read.
    Result<std::string> encode_error_response(const std::optional<protocol::RequestID>& id,
                                              const Error& error);

    template <typename T>
    Result<std::string> serialize_value(const T& value) {
        auto serialized = codec::json::to_string<lsp_config>(value);
        if(!serialized) {
            return outcome_error(
                Error(protocol::ErrorCode::InternalError, serialized.error().to_string()));
        }
        return std::move(*serialized);
    }

    template <typename T>
    Result<T> deserialize_value(std::string_view raw,
                                protocol::ErrorCode code = protocol::ErrorCode::RequestFailed) {
        if(raw.empty()) {
            if constexpr(std::is_same_v<T, protocol::null> ||
                         std::is_same_v<T, codec::dyn::Value>) {
                raw = "null";
            } else {
                raw = "{}";
            }
        }
        auto parsed = codec::json::from_string<T, lsp_config>(raw);
        if(!parsed) {
            return outcome_error(Error(code, parsed.error().to_string()));
        }
        return std::move(*parsed);
    }
};

using JsonPeer = Peer<JsonCodec>;

extern template class Peer<JsonCodec>;

}  // namespace kota::ipc
