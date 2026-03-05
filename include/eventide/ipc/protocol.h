#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "eventide/serde/serde.h"

namespace eventide::ipc::protocol {

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

struct RequestID {
    using value_type = std::int64_t;

    value_type value = 0;

    friend bool operator==(const RequestID&, const RequestID&) = default;
};

using ResponseID = std::optional<RequestID>;

enum class ErrorCode : integer {
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603,
    RequestFailed = -32000,
    RequestCancelled = -32800,
};

struct ResponseError {
    integer code = 0;
    string message;
    std::optional<Value> data = {};
};

}  // namespace eventide::ipc::protocol

namespace std {

template <>
struct hash<eventide::ipc::protocol::RequestID> {
    std::size_t operator()(const eventide::ipc::protocol::RequestID& id) const noexcept {
        return std::hash<eventide::ipc::protocol::RequestID::value_type>{}(id.value);
    }
};

}  // namespace std

namespace eventide::serde {

template <serializer_like S>
struct serialize_traits<S, eventide::ipc::protocol::RequestID> {
    using value_type = typename S::value_type;
    using error_type = typename S::error_type;

    static auto serialize(S& serializer, const eventide::ipc::protocol::RequestID& value)
        -> std::expected<value_type, error_type> {
        return serde::serialize(serializer, value.value);
    }
};

template <deserializer_like D>
struct deserialize_traits<D, eventide::ipc::protocol::RequestID> {
    using error_type = typename D::error_type;

    static auto deserialize(D& deserializer, eventide::ipc::protocol::RequestID& value)
        -> std::expected<void, error_type> {
        using namespace eventide::ipc::protocol;

        Variant variant{};
        auto status = serde::deserialize(deserializer, variant);
        if(!status) {
            return std::unexpected(status.error());
        }

        if(const auto* integer_id = std::get_if<std::int64_t>(&variant)) {
            value.value = *integer_id;
            return {};
        }

        if(const auto* unsigned_id = std::get_if<std::uint32_t>(&variant)) {
            value.value = static_cast<RequestID::value_type>(*unsigned_id);
            return {};
        }

        return request_id_type_mismatch();
    }

private:
    constexpr static auto request_id_type_mismatch() -> std::expected<void, error_type> {
        if constexpr(requires { error_type::type_mismatch; }) {
            return std::unexpected(error_type::type_mismatch);
        } else if constexpr(requires { error_type::invalid_type; }) {
            return std::unexpected(error_type::invalid_type);
        } else if constexpr(std::is_enum_v<error_type>) {
            return std::unexpected(static_cast<error_type>(1));
        } else {
            return std::unexpected(error_type{});
        }
    }
};

template <serializer_like S>
struct serialize_traits<S, eventide::ipc::protocol::Value> {
    using value_type = typename S::value_type;
    using error_type = typename S::error_type;

    static auto serialize(S& serializer, const eventide::ipc::protocol::Value& value)
        -> std::expected<value_type, error_type> {
        const auto& variant = static_cast<const eventide::ipc::protocol::Variant&>(value);
        return std::visit([&](const auto& item) { return serde::serialize(serializer, item); },
                          variant);
    }
};

template <deserializer_like D>
struct deserialize_traits<D, eventide::ipc::protocol::Value> {
    using error_type = typename D::error_type;

    static auto deserialize(D& deserializer, eventide::ipc::protocol::Value& value)
        -> std::expected<void, error_type> {
        eventide::ipc::protocol::Variant variant{};
        auto status = serde::deserialize(deserializer, variant);
        if(!status) {
            return std::unexpected(status.error());
        }
        std::visit([&](auto&& item) { value = std::forward<decltype(item)>(item); },
                   std::move(variant));
        return {};
    }
};

}  // namespace eventide::serde
