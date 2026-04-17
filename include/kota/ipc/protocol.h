#pragma once

#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "kota/codec/codec.h"

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

struct Value;

using Array = std::vector<Value>;
using Object = std::unordered_map<std::string, Value>;
using Variant = std::
    variant<Object, Array, std::string, std::int64_t, std::uint32_t, double, bool, std::nullptr_t>;

struct Value : Variant {
    using Variant::Variant;
    using Variant::operator=;
};

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
    std::optional<Value> data = {};

    Error() = default;

    Error(integer code, string message, std::optional<Value> data = {}) :
        code(code), message(std::move(message)), data(std::move(data)) {}

    Error(ErrorCode code, string message, std::optional<Value> data = {}) :
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

template <serializer_like S>
struct serialize_traits<S, kota::ipc::protocol::Value> {
    using value_type = typename S::value_type;
    using error_type = typename S::error_type;

    static auto serialize(S& serializer, const kota::ipc::protocol::Value& value)
        -> std::expected<value_type, error_type> {
        const auto& variant = static_cast<const kota::ipc::protocol::Variant&>(value);
        return std::visit([&](const auto& item) { return codec::serialize(serializer, item); },
                          variant);
    }
};

template <deserializer_like D>
struct deserialize_traits<D, kota::ipc::protocol::Value> {
    using error_type = typename D::error_type;

    static auto deserialize(D& deserializer, kota::ipc::protocol::Value& value)
        -> std::expected<void, error_type> {
        kota::ipc::protocol::Variant variant{};
        auto status = codec::deserialize(deserializer, variant);
        if(!status) {
            return std::unexpected(status.error());
        }
        std::visit([&](auto&& item) { value = std::forward<decltype(item)>(item); },
                   std::move(variant));
        return {};
    }
};

template <serializer_like S>
struct serialize_traits<S, kota::ipc::protocol::Error> {
    using value_type = typename S::value_type;
    using error_type = typename S::error_type;

    static auto serialize(S& s, const kota::ipc::protocol::Error& error)
        -> std::expected<value_type, error_type> {
        KOTA_EXPECTED_TRY_V(auto s_struct, s.serialize_struct("Error", 3));
        KOTA_EXPECTED_TRY(s_struct.serialize_field("code", error.code));
        KOTA_EXPECTED_TRY(s_struct.serialize_field("message", error.message));
        KOTA_EXPECTED_TRY(s_struct.serialize_field("data", error.data));
        return s_struct.end();
    }
};

template <deserializer_like D>
struct deserialize_traits<D, kota::ipc::protocol::Error> {
    using error_type = typename D::error_type;

    static auto deserialize(D& d, kota::ipc::protocol::Error& error)
        -> std::expected<void, error_type> {
        KOTA_EXPECTED_TRY_V(auto d_struct, d.deserialize_struct("Error", 3));
        while(true) {
            KOTA_EXPECTED_TRY_V(auto key, d_struct.next_key());
            if(!key.has_value())
                break;
            if(*key == "code") {
                KOTA_EXPECTED_TRY(d_struct.deserialize_value(error.code));
            } else if(*key == "message") {
                KOTA_EXPECTED_TRY(d_struct.deserialize_value(error.message));
            } else if(*key == "data") {
                KOTA_EXPECTED_TRY(d_struct.deserialize_value(error.data));
            }
        }
        return d_struct.end();
    }
};

}  // namespace kota::codec
