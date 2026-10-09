#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "kota/ipc/codec.h"
#include "kota/ipc/peer.h"
#include "kota/codec/json/json.h"

namespace kota::ipc {

struct JSONCodec {
    /// Parses payload in place: the bytes simdjson reads past the text are
    /// written after it while it parses, and cut off after, so no one else
    /// may hold or read it meanwhile.
    IncomingMessage parse_message(std::string& payload);

    IncomingMessage parse_message(std::string_view payload) {
        auto text = copy(payload);
        return parse_message(text);
    }

    /// Reads what it can from `prefix`, the first bytes of a message too
    /// large to read whole.
    MessageHead peek(std::string_view prefix);

    /// Empty params, those of a method that takes none, leave the member out.
    Result<std::string> encode_request(const protocol::RequestID& id,
                                       std::string_view method,
                                       std::string_view params);

    /// Empty params leave the member out, as for a request.
    Result<std::string> encode_notification(std::string_view method, std::string_view params);

    Result<std::string> encode_success_response(const protocol::RequestID& id,
                                                std::string_view result);

    /// Without an id, the error answers a message whose id could not be read.
    Result<std::string> encode_error_response(const std::optional<protocol::RequestID>& id,
                                              const Error& error);

    template <typename T>
    Result<std::string> serialize_value(const T& value) {
        return unwrap(codec::json::to_string<lsp_config>(value),
                      protocol::ErrorCode::InternalError);
    }

    /// Decodes raw in place, as parse_message parses a payload: raw grows
    /// while it decodes, and is cut back after, so no one else may hold or
    /// read it meanwhile. Empty raw, the params of a method that takes none,
    /// reads as null, or as an object without members.
    template <typename T>
    Result<T> deserialize_value(std::string& raw,
                                protocol::ErrorCode code = protocol::ErrorCode::RequestFailed) {
        if(raw.empty()) {
            constexpr bool null =
                std::is_same_v<T, protocol::null> || std::is_same_v<T, codec::dyn::Value>;
            return unwrap(codec::json::from_string<T, lsp_config>(null ? "null" : "{}"), code);
        }
        auto padded = pad(raw);
        return unwrap(codec::json::from_padded_string<T, lsp_config>(padded.view()), code);
    }

    template <typename T>
    Result<T> deserialize_value(std::string_view raw,
                                protocol::ErrorCode code = protocol::ErrorCode::RequestFailed) {
        auto text = copy(raw);
        return deserialize_value<T>(text, code);
    }

private:
    /// text with the bytes simdjson reads past it written after it, as
    /// spaces, while the Padded lives.
    struct Padded {
        std::string& text;
        std::size_t size;

        ~Padded() {
            text.resize(size);
        }

        codec::json::padded_string_view view() const {
            return codec::json::padded_string_view(text.data(), size, text.size());
        }
    };

    static Padded pad(std::string& text);

    /// text in a string with room for its padding.
    static std::string copy(std::string_view text);

    /// The peer error that carries a codec failure's message.
    static Error codec_error(protocol::ErrorCode code, const codec::rich_error& error);

    template <typename T>
    static Result<T> unwrap(std::expected<T, codec::rich_error> value, protocol::ErrorCode code) {
        if(!value) {
            return outcome_error(codec_error(code, value.error()));
        }
        return std::move(*value);
    }
};

using JSONPeer = Peer<JSONCodec>;

extern template class Peer<JSONCodec>;

}  // namespace kota::ipc
