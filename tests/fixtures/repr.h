#pragma once

// codec::repr fixtures meta's tests read as well: reprs whose declared type
// carries struct policies or a tagging spec, an adapter over a tagged
// variant, a repr scoped to every format tag, and a variant whose type has a
// repr under a tagging spec. The annotations are written as KANNOTATE and
// KANNOTATE_TYPE (in codec's macro sheet) expand to, so meta's tests can use
// them.

#include <charconv>
#include <cstdint>
#include <format>
#include <string>
#include <type_traits>
#include <variant>

#include "kotatsu/meta/annotation.h"
#include "kotatsu/codec/attribute.h"
#include "kotatsu/codec/format.h"
#include "kotatsu/codec/repr.h"

namespace kotatsu::test {

[[maybe_unused]] constexpr inline auto StrictCamelTag =
    meta::make_annotation(codec::dsl::rename_all = naming::Casing::LowerCamel,
                          codec::dsl::deny_unknown_fields = true);

/// A repr whose declared type is an adjacently tagged variant.
struct LoadOk {
    int byte_count = 0;

    auto operator==(const LoadOk&) const -> bool = default;
};

struct LoadErr {
    std::string message;

    auto operator==(const LoadErr&) const -> bool = default;
};

struct TaggedLoad;

constexpr auto __kotatsu__type_annotation(std::type_identity<TaggedLoad>) {
    return meta::make_annotation(codec::dsl::tag = "status",
                                 codec::dsl::content = "value",
                                 codec::dsl::tag_names = {"ok", "err"});
}

struct TaggedLoad : std::variant<LoadOk, LoadErr> {
    using variant::variant;
};

struct LoadResult {
    bool ok = true;
    int bytes = 0;
    std::string message;

    auto operator==(const LoadResult&) const -> bool = default;
};

/// Format-scoped: every backend with a format tag carries it as an integer;
/// the format-agnostic repr, which a backend without a tag sees, is "p<page>".
struct Journal {
    int page = 0;

    auto operator<=>(const Journal&) const = default;
};

/// Spells a variant as "i:<n>" or "s:<text>".
struct ChoiceText {
    using type = std::string;

    static std::string to(const std::variant<int, std::string>& v) {
        if(const auto* n = std::get_if<int>(&v)) {
            return std::format("i:{}", *n);
        }
        return std::format("s:{}", std::get<std::string>(v));
    }

    static std::variant<int, std::string> from(const std::string& encoded) {
        if(encoded.starts_with("i:")) {
            int n = 0;
            std::from_chars(encoded.data() + 2, encoded.data() + encoded.size(), n);
            return n;
        }
        return encoded.substr(2);
    }
};

/// A tagging spec and a with-adapter on one field: the adapter decides.
struct AdaptedChoice {
    static consteval auto __kotatsu__annotation(meta::name_key<"value">) {
        return meta::make_annotation(codec::dsl::tag = "t",
                                     codec::dsl::content = "c",
                                     codec::dsl::tag_names = {"num", "text"},
                                     codec::dsl::with = meta::dsl::type<ChoiceText>);
    }

    std::variant<int, std::string> value;
};

/// A variant whose type has a repr (one text), and the same variant under a
/// tagging spec: the tagging wins, so the tagged one travels as the variant.
struct Stamp {
    int n = 0;

    auto operator==(const Stamp&) const -> bool = default;
};

using StampOrNote = std::variant<Stamp, std::string>;

[[maybe_unused]] constexpr inline auto StampTag =
    meta::make_annotation(codec::dsl::tag = "t",
                          codec::dsl::content = "c",
                          codec::dsl::tag_names = {"stamp", "note"});

struct TaggedStampOrNote;

constexpr auto __kotatsu__type_annotation(std::type_identity<TaggedStampOrNote>) {
    return StampTag;
}

struct TaggedStampOrNote : StampOrNote {
    using variant::variant;
};

/// StampOrNote's shape without its repr.
struct StampTwin {
    int n = 0;
};

struct TaggedStampTwinOrNote;

constexpr auto __kotatsu__type_annotation(std::type_identity<TaggedStampTwinOrNote>) {
    return StampTag;
}

struct TaggedStampTwinOrNote : std::variant<StampTwin, std::string> {
    using variant::variant;
};

}  // namespace kotatsu::test

namespace kotatsu::codec {

template <>
struct repr<test::StampOrNote> {
    using type = std::string;

    static std::string to(const test::StampOrNote& v) {
        if(const auto* stamp = std::get_if<test::Stamp>(&v)) {
            return std::format("#{}", stamp->n);
        }
        return std::get<std::string>(v);
    }

    static test::StampOrNote from(const std::string& text) {
        if(text.starts_with("#")) {
            int n = 0;
            std::from_chars(text.data() + 1, text.data() + text.size(), n);
            return test::Stamp{.n = n};
        }
        return text;
    }
};

template <>
struct repr<test::LoadResult> {
    using type = test::TaggedLoad;

    static type to(const test::LoadResult& r) {
        if(r.ok) {
            return type{test::LoadOk{.byte_count = r.bytes}};
        }
        return type{test::LoadErr{.message = r.message}};
    }

    static test::LoadResult from(const type& tagged) {
        if(const auto* ok = std::get_if<test::LoadOk>(&tagged)) {
            return {.ok = true, .bytes = ok->byte_count, .message = {}};
        }
        return {.ok = false, .bytes = 0, .message = std::get<test::LoadErr>(tagged).message};
    }
};

template <>
struct repr<test::Journal> {
    using type = std::string;

    static type to(const test::Journal& j) {
        return std::format("p{}", j.page);
    }

    static test::Journal from(const std::string& encoded) {
        test::Journal j;
        std::from_chars(encoded.data() + 1, encoded.data() + encoded.size(), j.page);
        return j;
    }
};

/// Under a backend's format, not under the root format dyn reads and writes
/// trees with.
template <typename Format>
    requires (!std::is_void_v<Format> && !std::same_as<Format, codec::format>)
struct repr<test::Journal, Format> {
    using type = std::int64_t;

    static type to(const test::Journal& j) {
        return j.page;
    }

    static test::Journal from(type v) {
        return {.page = static_cast<int>(v)};
    }
};

}  // namespace kotatsu::codec
