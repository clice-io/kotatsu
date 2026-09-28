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

#include "kota/ipc/protocol.h"
#include "kota/meta/annotation.h"
#include "kota/meta/repr.h"
#include "kota/codec/dyn/dyn.h"

// The C++ spelling of the LSP metaModel's TypeScript type constructs, which
// the generated kota/ipc/lsp/protocol.h is written in.

namespace kota::ipc::protocol {

namespace detail {

struct skip_default_tag {
    constexpr static auto spec =
        meta::make_spec(meta::dsl::skip_if = meta::skip_when::default_value,
                        meta::dsl::defaulted = true);
};

struct skip_none_tag {
    constexpr static auto spec = meta::make_spec(meta::dsl::skip_if = meta::skip_when::none);
};

struct skip_none_defaulted_tag {
    constexpr static auto spec =
        meta::make_spec(meta::dsl::skip_if = meta::skip_when::none, meta::dsl::defaulted = true);
};

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
using optional_bool = meta::annotate<detail::skip_default_tag>::type<bool>;

/// `undefined | T`.
template <typename T>
using optional = meta::annotate<detail::skip_none_tag>::type<std::optional<T>>;

/// `undefined | T` for a T that has null among its values (a nullable<U>, or
/// LSPAny): absent is omitted, and null reads as a present null rather than as
/// absent.
template <typename T>
using optional_nullable =
    meta::annotate<detail::skip_none_defaulted_tag>::type<detail::present_optional<T>>;

/// `undefined | T` where T is the enclosing structure itself, which a
/// by-value member cannot hold.
template <typename T>
using optional_ptr = meta::annotate<detail::skip_default_tag>::type<std::unique_ptr<T>>;

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

}  // namespace kota::ipc::protocol

namespace kota::ipc::lsp {

namespace protocol = kota::ipc::protocol;

}  // namespace kota::ipc::lsp

namespace kota::codec::bincode {

struct format;

}  // namespace kota::codec::bincode

namespace kota::meta {

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

/// In bincode, which writes every field, the empty one too, as the
/// std::optional it is.
template <typename T>
struct repr<ipc::protocol::detail::present_optional<T>, codec::bincode::format> {
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

}  // namespace kota::meta

namespace kota::codec {

template <typename Vis, auto Text, typename Config>
struct serialize_visit<Vis, ipc::protocol::Literal<Text>, Config> {
    static bool visit(Vis& vis, const ipc::protocol::Literal<Text>&) {
        return encode_value<Config>(vis, ipc::protocol::Literal<Text>::value);
    }
};

template <typename Vis, auto Text, typename Config>
struct deserialize_visit<Vis, ipc::protocol::Literal<Text>, Config> {
    static bool visit(Vis& vis, ipc::protocol::Literal<Text>&) {
        constexpr auto expected = ipc::protocol::Literal<Text>::value;
        std::string text;
        KOTA_CODEC_TRY(decode_value<Config>(vis, text));
        if(text == expected) {
            return true;
        }
        return scoped_context<rich_error>::fail(
            rich_error(std::format(R"(expected "{}", got "{}")", expected, text)));
    }
};

}  // namespace kota::codec
