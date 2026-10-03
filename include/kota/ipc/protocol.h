#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "kota/codec/dyn/dyn.h"
#include "kota/codec/visit/config.h"
#include "kota/codec/visit/decode.h"
#include "kota/codec/visit/encode.h"

namespace kota::ipc::protocol {

template <typename Params>
struct RequestTraits;

template <typename Params>
struct NotificationTraits;

using boolean = bool;
using integer = std::int32_t;
using uinteger = std::uint32_t;
using decimal = double;
using string = std::string;
using null = std::nullptr_t;

using RequestID = std::variant<std::int64_t, std::string>;

enum class ErrorCode : integer {
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603,
    RequestFailed = -32000,
    /// A message larger than the transport reads: the request it was, or
    /// that it answered, fails with this.
    MessageTooLarge = -32010,
    RequestCancelled = -32800,
};

struct Error {
    integer code = static_cast<integer>(ErrorCode::RequestFailed);
    string message;
    std::optional<codec::dyn::Value> data = {};

    Error() = default;

    Error(integer code, string message, std::optional<codec::dyn::Value> data = {}) :
        code(code), message(std::move(message)), data(std::move(data)) {}

    Error(ErrorCode code, string message, std::optional<codec::dyn::Value> data = {}) :
        Error(static_cast<integer>(code), std::move(message), std::move(data)) {}

    Error(string message) : message(std::move(message)) {}

    Error(const char* message) : message(message == nullptr ? "" : message) {}
};

struct CancelRequestParams {
    RequestID id;
};

}  // namespace kota::ipc::protocol

namespace kota::codec {

template <typename Vis, typename Config>
struct serialize_visit<Vis, kota::ipc::protocol::Error, Config> {
    static bool visit(Vis& vis, const kota::ipc::protocol::Error& error) {
        return vis.visit_struct(error, [&]<typename StructVis>(StructVis& sv) -> bool {
            KOTA_CODEC_TRY(sv.visit_field(std::size_t(0), "code", [&](auto& fv) -> bool {
                return encode_value<Config>(fv, error.code);
            }));
            KOTA_CODEC_TRY(sv.visit_field(std::size_t(1), "message", [&](auto& fv) -> bool {
                return encode_value<Config>(fv, error.message);
            }));
            // JSON-RPC lets an error without data leave the member out; a
            // visitor that writes every field has nothing to mark it absent.
            if constexpr(!writes_every_field<StructVis>) {
                if(!error.data) {
                    return true;
                }
            }
            return sv.visit_field(std::size_t(2), "data", [&](auto& fv) -> bool {
                return encode_value<Config>(fv, error.data);
            });
        });
    }
};

template <typename Vis, typename Config>
struct deserialize_visit<Vis, kota::ipc::protocol::Error, Config> {
    static bool visit(Vis& vis, kota::ipc::protocol::Error& error) {
        return vis.visit_struct([&](std::string_view key, auto& fv) -> bool {
            if(key == "code") {
                return decode_value<Config>(fv, error.code);
            } else if(key == "message") {
                return decode_value<Config>(fv, error.message);
            } else if(key == "data") {
                return decode_value<Config>(fv, error.data);
            } else {
                return true;
            }
        });
    }
};

}  // namespace kota::codec
