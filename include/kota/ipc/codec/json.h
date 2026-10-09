#pragma once

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

/// JSON-RPC messages in JSON, members named as LSP names them. A value goes
/// into a message encoded as what it is: JSON text encoded already goes as a
/// codec::RawValue, as it is, where a string goes as a JSON string.
struct JSONCodec {
    /// Reads the message `payload` holds. The params or result keep the
    /// payload instead of a copy of their text.
    IncomingMessage parse_message(std::string payload);

    /// Reads what it can from `prefix`, the first bytes of a message too
    /// large to read whole.
    MessageHead peek(std::string_view prefix);

    /// A request for a method that takes no params: the member is left out.
    Result<std::string> encode_request(const protocol::RequestID& id, std::string_view method);

    template <typename Params>
    Result<std::string> encode_request(const protocol::RequestID& id,
                                       std::string_view method,
                                       const Params& params) {
        MessageWriter out;
        out.text(R"({"jsonrpc":"2.0","id":)").value(id).text(R"(,"method":)").value(method);
        out.text(R"(,"params":)").value(params).text("}");
        return std::move(out).finish();
    }

    /// A notification of a method that takes no params, without the member.
    Result<std::string> encode_notification(std::string_view method);

    template <typename Params>
    Result<std::string> encode_notification(std::string_view method, const Params& params) {
        MessageWriter out;
        out.text(R"({"jsonrpc":"2.0","method":)").value(method);
        out.text(R"(,"params":)").value(params).text("}");
        return std::move(out).finish();
    }

    /// An empty RawValue result, which no JSON is, is written as null: the
    /// result of a method that returns nothing.
    template <typename T>
    Result<std::string> encode_success_response(const protocol::RequestID& id, const T& result) {
        MessageWriter out;
        out.text(R"({"jsonrpc":"2.0","id":)").value(id).text(R"(,"result":)").value(result);
        out.text("}");
        return std::move(out).finish();
    }

    /// Without an id, the error answers a message whose id could not be read.
    Result<std::string> encode_error_response(const std::optional<protocol::RequestID>& id,
                                              const Error& error);

    /// Decodes the slice in place, writing simdjson's padding after it; a
    /// RawValue takes its text as it is, checked as parse_message checks a
    /// message. Empty params, those of a method that takes none, read as
    /// null, or as an object without members.
    template <typename T>
    Result<T> deserialize_value(PayloadSlice& raw,
                                protocol::ErrorCode code = protocol::ErrorCode::RequestFailed) {
        if(raw.size == 0) {
            constexpr bool null =
                std::is_same_v<T, protocol::null> || std::is_same_v<T, codec::dyn::Value>;
            return unwrap(codec::json::from_string<T, lsp_config>(null ? "null" : "{}"), code);
        }
        if constexpr(std::is_same_v<T, codec::RawValue>) {
            return codec::RawValue{std::string(raw.text())};
        } else {
            return unwrap(codec::json::from_padded_string<T, lsp_config>(pad(raw)), code);
        }
    }

private:
    /// Writes a message, its text and its values in the codec's config, into
    /// one buffer, so that no value is copied into its message.
    struct MessageWriter {
        codec::rich_error error;
        codec::scoped_context<codec::rich_error> guard{error};
        codec::json::StringBuilder builder{codec::json::default_capacity};
        codec::json::ValueWriter values{builder};
        bool written = true;

        MessageWriter& text(std::string_view raw) {
            builder.append_raw(raw);
            return *this;
        }

        template <typename T>
        MessageWriter& value(const T& member) {
            written =
                written && codec::encode_value<codec::default_config<lsp_config>>(values, member);
            return *this;
        }

        Result<std::string> finish() && {
            if(!written) {
                return outcome_error(codec_error(protocol::ErrorCode::InternalError, error));
            }
            return std::move(builder).take();
        }
    };

    /// The slice with the bytes simdjson reads past it written after it, as
    /// spaces.
    static codec::json::padded_string_view pad(PayloadSlice& slice);

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
