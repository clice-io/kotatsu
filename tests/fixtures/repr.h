#pragma once

// meta::repr fixtures meta's tests read as well: reprs whose declared type
// carries a behavior attr, struct policies or a tagging spec, an adapter
// over a tagged variant, and a repr scoped to every format tag.

#include <charconv>
#include <cstdint>
#include <format>
#include <string>
#include <type_traits>
#include <variant>

#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"
#include "kota/meta/repr.h"

namespace kota::test {

/// A repr whose declared type is itself annotated: the annotation's behavior
/// attr decides the final repr, a double.
struct BasisPoints {
    int v = 0;

    auto operator==(const BasisPoints&) const -> bool = default;
};

struct StrictCamelTag {
    constexpr static auto spec =
        meta::make_struct_spec(meta::dsl::rename_all = naming::casing::lower_camel,
                               meta::dsl::deny_unknown_fields = true);
};

/// A repr whose declared type is an adjacently tagged variant.
struct LoadOk {
    int byte_count = 0;

    auto operator==(const LoadOk&) const -> bool = default;
};

struct LoadErr {
    std::string message;

    auto operator==(const LoadErr&) const -> bool = default;
};

struct LoadTag {
    constexpr static auto spec = meta::make_struct_spec(meta::dsl::tag = "status",
                                                        meta::dsl::content = "value",
                                                        meta::dsl::tag_names = {"ok", "err"});
};

using TaggedLoad = meta::annotate<LoadTag>::type<std::variant<LoadOk, LoadErr>>;

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

struct ChoiceTag {
    constexpr static auto spec = meta::make_struct_spec(meta::dsl::tag = "t",
                                                        meta::dsl::content = "c",
                                                        meta::dsl::tag_names = {"num", "text"});
};

/// A tagging spec and a with-adapter on one annotation: the adapter decides.
using AdaptedChoice = meta::annotate<ChoiceTag>::type<std::variant<int, std::string>,
                                                      meta::behavior::with<ChoiceText>>;

}  // namespace kota::test

namespace kota::meta {

template <>
struct repr<test::BasisPoints> {
    using type = annotation<int, behavior::as<double>>;

    static type to(const test::BasisPoints& b) {
        return b.v;
    }

    static test::BasisPoints from(const type& v) {
        return {.v = annotated_value(v)};
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
        const auto& v = annotated_value(tagged);
        if(const auto* ok = std::get_if<test::LoadOk>(&v)) {
            return {.ok = true, .bytes = ok->byte_count, .message = {}};
        }
        return {.ok = false, .bytes = 0, .message = std::get<test::LoadErr>(v).message};
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

template <typename Format>
    requires (!std::is_void_v<Format>)
struct repr<test::Journal, Format> {
    using type = std::int64_t;

    static type to(const test::Journal& j) {
        return j.page;
    }

    static test::Journal from(type v) {
        return {.page = static_cast<int>(v)};
    }
};

}  // namespace kota::meta
