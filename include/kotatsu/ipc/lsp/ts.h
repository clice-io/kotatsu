#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "kotatsu/ipc/protocol.h"
#include "kotatsu/meta/annotation.h"
#include "kotatsu/codec/dyn/dyn.h"
#include "kotatsu/codec/repr.h"

// The C++ spelling of the LSP metaModel's TypeScript type constructs, which
// the generated kotatsu/ipc/lsp/protocol.h is written in.

namespace kotatsu::ipc::protocol {

namespace detail {

template <std::size_t N>
struct LiteralText {
    char text[N];

    consteval LiteralText(const char (&value)[N]) {
        std::copy_n(value, N, text);
    }
};

}  // namespace detail

/// `T | null`: null or a value, but present. Unlike a plain std::optional, a
/// structure whose required property of this type is missing fails to read.
template <typename T>
struct nullable : std::optional<T> {
    using std::optional<T>::optional;
    using std::optional<T>::operator=;

    constexpr nullable() = default;

    constexpr nullable(std::optional<T> value) : std::optional<T>(std::move(value)) {}

    /// std::optional's comparisons all match two of these, none better.
    template <std::same_as<nullable> L, std::same_as<nullable> R>
        requires requires(const std::optional<T>& value) { value == value; }
    friend constexpr bool operator==(const L& lhs, const R& rhs) {
        return static_cast<const std::optional<T>&>(lhs) ==
               static_cast<const std::optional<T>&>(rhs);
    }
};

namespace detail {

/// A std::optional<T> whose value is present even when it is null, for a T
/// that has null among its values.
template <typename T>
struct present_optional : std::optional<T> {
    using std::optional<T>::optional;
    using std::optional<T>::operator=;

    /// std::optional's comparisons all match two of these, none better.
    template <std::same_as<present_optional> L, std::same_as<present_optional> R>
        requires requires(const std::optional<T>& value) { value == value; }
    friend constexpr bool operator==(const L& lhs, const R& rhs) {
        return static_cast<const std::optional<T>&>(lhs) ==
               static_cast<const std::optional<T>&>(rhs);
    }
};

}  // namespace detail

/// `undefined | boolean` where absent means false: false is omitted. The
/// booleans for which absent means something else are `optional<boolean>`.
struct optional_bool {
    bool value = false;

    constexpr optional_bool() = default;

    constexpr optional_bool(bool value) : value(value) {}

    constexpr explicit operator bool() const {
        return value;
    }

    constexpr bool operator==(const optional_bool&) const = default;
};

consteval auto __kotatsu__type_annotation(std::type_identity<optional_bool>) {
    return meta::make_annotation(codec::dsl::presence = codec::presence_kind::omit_if_default);
}

/// `undefined | T`.
template <typename T>
struct optional : std::optional<T> {
    using std::optional<T>::optional;
    using std::optional<T>::operator=;

    constexpr optional() = default;

    constexpr optional(std::optional<T> value) : std::optional<T>(std::move(value)) {}

    /// std::optional's comparisons all match two of these, none better.
    template <std::same_as<optional> L, std::same_as<optional> R>
        requires requires(const std::optional<T>& value) { value == value; }
    friend constexpr bool operator==(const L& lhs, const R& rhs) {
        return static_cast<const std::optional<T>&>(lhs) ==
               static_cast<const std::optional<T>&>(rhs);
    }
};

template <typename T>
consteval auto __kotatsu__type_annotation(std::type_identity<optional<T>>) {
    return meta::make_annotation(codec::dsl::presence = codec::presence_kind::omit_if_default);
}

namespace detail {

template <typename T>
consteval auto __kotatsu__type_annotation(std::type_identity<present_optional<T>>) {
    return meta::make_annotation(codec::dsl::presence = codec::presence_kind::omit_if_default);
}

}  // namespace detail

/// `undefined | T` for a T that has null among its values (a nullable<U>, or
/// LSPAny): absent is omitted, and null reads as a present null rather than as
/// absent.
template <typename T>
using optional_nullable = detail::present_optional<T>;

/// `undefined | T` where T is the enclosing structure itself, which a
/// by-value member cannot hold.
template <typename T>
struct optional_ptr : std::unique_ptr<T> {
    using std::unique_ptr<T>::unique_ptr;

    constexpr optional_ptr() = default;

    optional_ptr(std::unique_ptr<T> value) : std::unique_ptr<T>(std::move(value)) {}
};

template <typename T>
consteval auto __kotatsu__type_annotation(std::type_identity<optional_ptr<T>>) {
    return meta::make_annotation(codec::dsl::presence = codec::presence_kind::omit_if_default);
}

/// `A | B | ...`, decoded as the first alternative the input fits.
using std::variant;

/// The empty object literal `{}`.
struct EmptyObject {};

/// A string literal type such as `kind: 'create'`. It holds nothing: encoding
/// writes the text and decoding accepts only the text, so the alternatives of
/// a variant are told apart by it.
template <detail::LiteralText Text>
struct Literal {
    constexpr static std::string_view value{Text.text, sizeof(Text.text) - 1};
};

using URI = string;
using DocumentUri = string;

}  // namespace kotatsu::ipc::protocol

namespace kotatsu::ipc::lsp {

namespace protocol = kotatsu::ipc::protocol;

}  // namespace kotatsu::ipc::lsp

namespace kotatsu::codec {

/// Read and written as the std::optional it is; being no std::optional to the
/// codec, it is required in a structure.
template <typename T>
struct repr<ipc::protocol::nullable<T>> {
    using type = std::optional<T>;

    const static type& to(const ipc::protocol::nullable<T>& value) {
        return value;
    }

    static ipc::protocol::nullable<T> from(type value) {
        return value;
    }
};

/// Read and written as the bool it is.
template <>
struct repr<ipc::protocol::optional_bool> {
    using type = bool;

    static type to(const ipc::protocol::optional_bool& value) {
        return value.value;
    }

    static ipc::protocol::optional_bool from(type value) {
        return value;
    }
};

/// Read and written as the std::optional it is.
template <typename T>
struct repr<ipc::protocol::optional<T>> {
    using type = std::optional<T>;

    const static type& to(const ipc::protocol::optional<T>& value) {
        return value;
    }

    static ipc::protocol::optional<T> from(type value) {
        return value;
    }
};

/// Read and written as the std::unique_ptr it is.
template <typename T>
struct repr<ipc::protocol::optional_ptr<T>> {
    using type = std::unique_ptr<T>;

    const static type& to(const ipc::protocol::optional_ptr<T>& value) {
        return value;
    }

    static ipc::protocol::optional_ptr<T> from(type value) {
        return value;
    }
};

/// Read and written as its value, so that null reaches T; absence is the
/// field's, which optional_nullable leaves out, so it is never written empty.
template <typename T>
struct repr<ipc::protocol::detail::present_optional<T>> {
    using type = T;

    const static type& to(const ipc::protocol::detail::present_optional<T>& value) {
        return *value;
    }

    static ipc::protocol::detail::present_optional<T> from(type value) {
        return ipc::protocol::detail::present_optional<T>(std::move(value));
    }
};

/// In a format that writes every field, the empty one too (bincode, fbs), as
/// the std::optional it is.
template <typename T, typename F>
    requires requires { requires F::writes_every_field; }
struct repr<ipc::protocol::detail::present_optional<T>, F> {
    using type = std::optional<T>;

    const static type& to(const ipc::protocol::detail::present_optional<T>& value) {
        return value;
    }

    static ipc::protocol::detail::present_optional<T> from(type value) {
        ipc::protocol::detail::present_optional<T> read;
        if(value) {
            read = std::move(*value);
        }
        return read;
    }
};

}  // namespace kotatsu::codec

namespace kotatsu::codec {

template <typename Vis, auto Text>
struct serialize_visit<Vis, ipc::protocol::Literal<Text>> {
    static bool visit(Vis& vis, const ipc::protocol::Literal<Text>&) {
        return encode_value(vis, ipc::protocol::Literal<Text>::value);
    }
};

template <typename Vis, auto Text>
struct deserialize_visit<Vis, ipc::protocol::Literal<Text>> {
    static bool visit(Vis& vis, ipc::protocol::Literal<Text>&) {
        constexpr auto expected = ipc::protocol::Literal<Text>::value;
        std::string text;
        KOTATSU_CODEC_TRY(decode_value(vis, text));
        if(text == expected) {
            return true;
        }
        return scoped_context<rich_error>::fail(
            rich_error(std::format(R"(expected "{}", got "{}")", expected, text)));
    }
};

}  // namespace kotatsu::codec
