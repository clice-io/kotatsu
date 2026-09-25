#pragma once

#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>
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

namespace std {

template <>
struct hash<kota::ipc::protocol::RequestID> {
    std::size_t operator()(const kota::ipc::protocol::RequestID& id) const noexcept {
        return std::visit(
            [](const auto& v) -> std::size_t {
                return std::hash<std::remove_cvref_t<decltype(v)>>{}(v);
            },
            id);
    }
};

template <>
struct formatter<kota::ipc::protocol::RequestID> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }

    auto format(const kota::ipc::protocol::RequestID& id, format_context& ctx) const {
        return std::visit(
            [&](const auto& v) {
                if constexpr(std::is_same_v<std::remove_cvref_t<decltype(v)>, std::string>) {
                    return std::format_to(ctx.out(), "\"{}\"", v);
                } else {
                    return std::format_to(ctx.out(), "{}", v);
                }
            },
            id);
    }
};

}  // namespace std

namespace kota::codec {

template <typename Vis, typename Config>
struct serialize_visit<Vis, kota::ipc::protocol::Error, Config> {
    static bool visit(Vis& vis, const kota::ipc::protocol::Error& error) {
        return vis.visit_struct(error, [&](auto& sv) -> bool {
            KOTA_CODEC_TRY(sv.visit_field(std::size_t(0), "code", [&](auto& fv) -> bool {
                return encode_value<Config>(fv, error.code);
            }));
            KOTA_CODEC_TRY(sv.visit_field(std::size_t(1), "message", [&](auto& fv) -> bool {
                return encode_value<Config>(fv, error.message);
            }));
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
                return fv.visit_skip();
            }
        });
    }
};

}  // namespace kota::codec
