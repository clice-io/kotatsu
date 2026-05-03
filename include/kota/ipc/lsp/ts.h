#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "kota/ipc/protocol.h"

namespace kota::ipc::protocol {

/// For `undefined | bool` .
using optional_bool = meta::skip_if_default<bool>;

/// For `undefined | T` .
template <typename T>
using optional = meta::skip_if_none<T>;

/// For `a: T | null`
template <typename T>
using nullable = std::optional<T>;

/// For `A | B | ...`
using std::variant;

/// For `undefined | (A | B | ...)`
template <typename... Ts>
using optional_variant = optional<variant<Ts...>>;

/// For multiple inherit.
using meta::flatten;

/// For closed string enum.
using meta::enum_string;

/// For empty object literal.
struct LspEmptyObject {};

/// The LSP any type.
/// Please note that strictly speaking a property with the value `undefined`
/// can't be converted into JSON preserving the property name. However for
/// convenience it is allowed and assumed that all these properties are
/// optional as well.
/// @since 3.17.0
struct LSPAny;

/// LSP arrays.
/// @since 3.17.0
using LSPArray = std::vector<LSPAny>;

/// LSP object definition.
/// @since 3.17.0
using LSPObject = std::unordered_map<std::string, LSPAny>;

using LSPVariant = std::variant<LSPObject,
                                LSPArray,
                                std::string,
                                std::int64_t,
                                std::uint32_t,
                                double,
                                bool,
                                std::nullptr_t>;

struct LSPAny : LSPVariant {
    using LSPVariant::LSPVariant;
    using LSPVariant::operator=;
};

struct LSPEmpty {};

using URI = string;
using DocumentUri = string;

struct IncomingMessage {
    optional<string> method;
    RequestID id;
    optional<string> params_json;
    optional<string> result_json;
    optional<string> error_json;
};

}  // namespace kota::ipc::protocol

namespace kota::ipc::lsp {

namespace protocol = kota::ipc::protocol;

}  // namespace kota::ipc::lsp

namespace kota::codec {

template <serializer_like S>
struct serialize_traits<S, kota::ipc::protocol::LSPAny> {
    using value_type = typename S::value_type;
    using error_type = typename S::error_type;

    static auto serialize(S& serializer, const kota::ipc::protocol::LSPAny& value)
        -> std::expected<value_type, error_type> {
        const auto& variant = static_cast<const kota::ipc::protocol::LSPVariant&>(value);
        return std::visit([&](const auto& item) { return codec::serialize(serializer, item); },
                          variant);
    }
};

}  // namespace kota::codec
