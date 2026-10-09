#pragma once

#include <cstddef>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "kota/ipc/codec.h"
#include "kota/ipc/peer.h"
#include "kota/codec/bincode/bincode.h"

namespace kota::codec {

// Bincode serialization: write int64 directly (bincode only uses integer IDs)
template <typename Config>
struct serialize_visit<bincode::Writer, kota::ipc::protocol::RequestID, Config> {
    static bool visit(bincode::Writer& vis, const kota::ipc::protocol::RequestID& id) {
        auto* int_id = std::get_if<std::int64_t>(&id);
        if(!int_id) {
            return scoped_context<rich_error>::fail(
                rich_error("bincode requires integer request ID"));
        }
        return encode_value<Config>(vis, *int_id);
    }
};

// Bincode deserialization: read int64 directly (visitor-based)
template <typename Config>
struct deserialize_visit<bincode::Reader, kota::ipc::protocol::RequestID, Config> {
    static bool visit(bincode::Reader& vis, kota::ipc::protocol::RequestID& id) {
        std::int64_t v = 0;
        if(!decode_value<Config>(vis, v)) {
            return false;
        }
        id.emplace<std::int64_t>(v);
        return true;
    }
};

}  // namespace kota::codec

namespace kota::ipc {

namespace detail {

// A message's fields before its params or result, which run from them to the
// end of the payload, as the codec wrote them; the alternatives in the order
// of the message kinds, whose index comes first. An error, the fourth kind,
// has no params or result.

struct request_prefix {
    protocol::RequestID id;
    std::string_view method;
};

struct notification_prefix {
    std::string_view method;
};

struct success_prefix {
    protocol::RequestID id;
};

using message_prefix = std::variant<request_prefix, notification_prefix, success_prefix>;

}  // namespace detail

/// Messages in bincode. A message's params or result take the rest of its
/// payload, without a length; a RawValue is taken to be bincode already, and
/// goes in and comes out as it is.
struct BincodeCodec {
    /// Reads the message `payload` holds. The params or result keep the
    /// payload instead of a copy of their bytes.
    IncomingMessage parse_message(std::string payload);

    /// Reads what it can from `prefix`, the first bytes of a message too
    /// large to read whole.
    MessageHead peek(std::string_view prefix);

    /// A request for a method that takes no params, which are left empty.
    Result<std::string> encode_request(const protocol::RequestID& id, std::string_view method) {
        return message(detail::request_prefix{.id = id, .method = method});
    }

    template <typename Params>
    Result<std::string> encode_request(const protocol::RequestID& id,
                                       std::string_view method,
                                       const Params& params) {
        return message(detail::request_prefix{.id = id, .method = method}, &params);
    }

    /// A notification of a method that takes no params, which are left
    /// empty.
    Result<std::string> encode_notification(std::string_view method) {
        return message(detail::notification_prefix{.method = method});
    }

    template <typename Params>
    Result<std::string> encode_notification(std::string_view method, const Params& params) {
        return message(detail::notification_prefix{.method = method}, &params);
    }

    template <typename T>
    Result<std::string> encode_success_response(const protocol::RequestID& id, const T& result) {
        return message(detail::success_prefix{.id = id}, &result);
    }

    /// Without an id, the error answers a message whose id could not be read.
    Result<std::string> encode_error_response(const std::optional<protocol::RequestID>& id,
                                              const Error& error);

    /// Empty bytes decode only into a value without fields, as params
    /// without fields are written.
    template <typename T>
    Result<T> deserialize_value(const PayloadSlice& raw,
                                protocol::ErrorCode code = protocol::ErrorCode::RequestFailed) {
        if constexpr(std::is_same_v<T, codec::RawValue>) {
            return codec::RawValue{std::string(raw.text())};
        } else {
            const auto text = raw.text();
            T value{};
            auto status = codec::bincode::from_bytes(
                std::span(reinterpret_cast<const std::byte*>(text.data()), text.size()),
                value);
            if(!status) {
                return outcome_error(codec_error(code, status.error()));
            }
            return value;
        }
    }

private:
    /// The peer error that carries a codec failure's message.
    static Error codec_error(protocol::ErrorCode code, const codec::rich_error& error);

    /// A message: prefix, then what value points to, if anything, written
    /// into the same buffer.
    template <typename T = void>
    static Result<std::string> message(const detail::message_prefix& prefix,
                                       const T* value = nullptr) {
        codec::rich_error error;
        codec::scoped_context<codec::rich_error> guard(error);
        std::vector<std::byte> bytes;
        codec::bincode::Writer writer{bytes};
        bool written = codec::encode_value<codec::default_config<>>(writer, prefix);
        if constexpr(std::is_same_v<T, codec::RawValue>) {
            std::memcpy(writer.claim(value->data.size()), value->data.data(), value->data.size());
        } else if constexpr(!std::is_void_v<T>) {
            written = written && codec::encode_value<codec::default_config<>>(writer, *value);
        }
        if(!written) {
            return outcome_error(codec_error(protocol::ErrorCode::InternalError, error));
        }
        return std::string(reinterpret_cast<const char*>(bytes.data()), writer.size);
    }
};

using BincodePeer = Peer<BincodeCodec>;

extern template class Peer<BincodeCodec>;

}  // namespace kota::ipc
