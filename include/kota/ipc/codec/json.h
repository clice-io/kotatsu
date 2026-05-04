#pragma once

#include <type_traits>

#include "kota/ipc/codec.h"
#include "kota/ipc/peer.h"
#include "kota/codec/detail/config.h"
#include "kota/codec/detail/raw_value.h"
#include "kota/codec/detail/spelling.h"
#include "kota/codec/json/json.h"

namespace kota::codec {

/// deserialize_traits for protocol::Value: deserialize as underlying Variant
template <>
struct deserialize_traits<json::simdjson_backend, ipc::protocol::Value> {
    using Backend = json::simdjson_backend;

    static auto read(Backend::value_type& val, ipc::protocol::Value& out) -> Backend::error_type {
        ipc::protocol::Variant variant{};
        auto err = codec::deserialize<Backend>(val, variant);
        if(err != Backend::success)
            return err;
        std::visit([&](auto&& item) { out = std::forward<decltype(item)>(item); },
                   std::move(variant));
        return Backend::success;
    }
};

/// deserialize_traits for protocol::Error: manual field-by-field deserialization
template <>
struct deserialize_traits<json::simdjson_backend, ipc::protocol::Error> {
    using Backend = json::simdjson_backend;

    static auto read(Backend::value_type& val, ipc::protocol::Error& out) -> Backend::error_type {
        struct error_visitor {
            ipc::protocol::Error& out;

            Backend::error_type visit_field(std::string_view key, Backend::value_type& field_val) {
                if(key == "code") {
                    return codec::deserialize<Backend>(field_val, out.code);
                } else if(key == "message") {
                    return codec::deserialize<Backend>(field_val, out.message);
                } else if(key == "data") {
                    return codec::deserialize<Backend>(field_val, out.data);
                }
                return Backend::success;
            }
        };

        error_visitor vis{out};
        return Backend::visit_object(val, vis);
    }
};

}  // namespace kota::codec

namespace kota::ipc {

struct lsp_config {
    using field_rename = codec::rename_policy::lower_camel;
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
            if constexpr(std::is_same_v<T, protocol::null> || std::is_same_v<T, protocol::Value>) {
                raw = "null";
            } else {
                raw = "{}";
            }
        }
        auto parsed = codec::json::parse<T, lsp_config>(raw);
        if(!parsed) {
            return outcome_error(Error(code, parsed.error().to_string()));
        }
        return std::move(*parsed);
    }
};

using JsonPeer = Peer<JsonCodec>;

extern template class Peer<JsonCodec>;

}  // namespace kota::ipc
