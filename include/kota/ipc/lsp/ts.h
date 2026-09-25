#pragma once

#include <algorithm>
#include <cstddef>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

#include "kota/ipc/protocol.h"
#include "kota/meta/annotation.h"
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

template <std::size_t N>
struct LiteralText {
    char text[N];

    consteval LiteralText(const char (&value)[N]) {
        std::copy_n(value, N, text);
    }
};

}  // namespace detail

/// `undefined | boolean`: absent reads as false and false is omitted.
using optional_bool = meta::annotate<detail::skip_default_tag>::type<bool>;

/// `undefined | T`.
template <typename T>
using optional = meta::annotate<detail::skip_none_tag>::type<std::optional<T>>;

/// `undefined | T` where T is the enclosing structure itself, which a
/// by-value member cannot hold.
template <typename T>
using optional_ptr = meta::annotate<detail::skip_default_tag>::type<std::unique_ptr<T>>;

/// `T | null`.
template <typename T>
using nullable = std::optional<T>;

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
        return scoped_context<typename Vis::error_type>::fail(
            rich_error(std::format(R"(expected "{}", got "{}")", expected, text)));
    }
};

}  // namespace kota::codec
