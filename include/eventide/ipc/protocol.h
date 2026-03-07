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

    enum class State : std::uint8_t { absent, null, integer };

    State state = State::absent;
    value_type value = 0;

    RequestID() = default;

    explicit RequestID(value_type v) : state(State::integer), value(v) {}

    bool has_value() const noexcept {
        return state == State::integer;
    }

    bool is_null() const noexcept {
        return state == State::null;
    }

    bool is_absent() const noexcept {
        return state == State::absent;
    }

    static RequestID null_id() {
        RequestID id;
        id.state = State::null;
        return id;
    }

    friend bool operator==(const RequestID&, const RequestID&) = default;
};

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
        auto h1 = std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(id.state));
        auto h2 = std::hash<eventide::ipc::protocol::RequestID::value_type>{}(id.value);
        return h1 ^ (h2 << 1);
    }
};

}  // namespace std

namespace eventide::serde {

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
