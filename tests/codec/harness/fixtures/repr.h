#pragma once

// meta::repr fixtures: every form of the repr protocol, and the plain structs
// that describe the documents they encode to.

#include <cctype>
#include <charconv>
#include <compare>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"
#include "kota/meta/repr.h"
#include "kota/meta/type_kind.h"
#include "kota/codec/visit/encode.h"

namespace kota::test {

/// Travels as a fixed-width integer, overriding the enum dispatch.
enum class Relation : std::uint8_t {
    declares,
    defines,
    references,
};

/// Travels as "major.minor".
struct Version {
    int major = 0;
    int minor = 0;

    auto operator<=>(const Version&) const = default;
};

/// Encode-only: decoding it is never instantiated.
struct AuditStamp {
    std::uint64_t at = 0;
};

/// Imperative form: the repr's body drives the visitor.
struct HexId {
    std::uint32_t v = 0;

    auto operator==(const HexId&) const -> bool = default;
};

/// Dynamic form: an integer or a string, decided at run time.
struct DynamicBox {
    std::variant<std::int64_t, std::string> v;

    auto operator==(const DynamicBox&) const -> bool = default;
};

/// A chained repr: Ticket travels as StepId, whose own repr is uint32.
struct StepId {
    std::uint32_t v = 0;

    auto operator==(const StepId&) const -> bool = default;
};

struct Ticket {
    StepId id;

    auto operator==(const Ticket&) const -> bool = default;
};

/// A value whose repr is nullable: zero travels as null.
struct Lamport {
    std::uint32_t tick = 0;

    auto operator==(const Lamport&) const -> bool = default;
};

/// A repr whose declared type is itself annotated: the annotation's behavior
/// attr decides the final repr, a double.
struct BasisPoints {
    int v = 0;

    auto operator==(const BasisPoints&) const -> bool = default;
};

/// A repr whose declared type is an annotated struct: its rename_all and
/// deny_unknown_fields shape the document.
struct LineRange {
    int first = 0;
    int last = 0;

    auto operator==(const LineRange&) const -> bool = default;
};

struct LineSpan {
    int start_line = 0;
    int line_count = 0;
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

/// A repr that declares an untagged variant: probing flows through it into
/// the variant.
struct BoxedScalar {
    std::variant<std::int8_t, double> v;

    auto operator==(const BoxedScalar&) const -> bool = default;
};

/// Adapters for behavior::with.
struct VersionAsNumber {
    using type = std::uint32_t;

    static std::uint32_t to(const Version& v) {
        return static_cast<std::uint32_t>(v.major * 1000 + v.minor);
    }

    static Version from(std::uint32_t encoded) {
        return {.major = static_cast<int>(encoded / 1000),
                .minor = static_cast<int>(encoded % 1000)};
    }
};

/// Imperative: uppercases when encoding, lowercases back.
struct Shout {
    using type = std::string;

    template <typename Config>
    static bool serialize(auto& vis, const std::string& text) {
        std::string encoded = text;
        for(char& c: encoded) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        return vis.visit_str(encoded);
    }

    template <typename Config>
    static bool deserialize(auto& vis, std::string& text) {
        std::string encoded;
        if(!vis.visit_str(encoded)) {
            return false;
        }
        for(char& c: encoded) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        text = std::move(encoded);
        return true;
    }
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

// Holders of values under a repr, and the plain structs of their documents.

struct Symbol {
    Relation rel;
    Version ver;
};

struct SymbolPlain {
    std::uint32_t rel;
    std::string ver;
};

/// `maybe` starts engaged, so a null that did not reset it shows.
struct ReprPlaces {
    std::vector<Relation> relations;
    std::map<Version, int> by_version;
    std::optional<Version> maybe = Version{.major = 9, .minor = 9};
};

struct ReprPlacesPlain {
    std::vector<std::uint32_t> relations;
    std::map<std::string, int> by_version;
    std::optional<std::string> maybe;
};

/// `stamp` starts nonzero, so a null read that did not reach Lamport's repr
/// shows.
struct Stamped {
    Lamport stamp{.tick = 9};
};

struct StampedPlain {
    std::optional<std::uint32_t> stamp;
};

struct DynamicPair {
    DynamicBox number;
    DynamicBox text;
};

struct DynamicPairPlain {
    std::int64_t number;
    std::string text;
};

struct LineSpanCamel {
    int startLine;
    int lineCount;
};

struct LineSpanCamelWithExtra {
    int startLine;
    int lineCount;
    int x;
};

struct LoadDocument {
    std::string status;
    LoadErr value;
};

struct ByteCountCamel {
    int byteCount;
};

struct ByteCountCamelWithExtra {
    int byteCount;
    int x;
};

template <typename Value>
struct LoadOkDocument {
    std::string status;
    Value value;
};

}  // namespace kota::test

namespace kota::meta {

template <>
struct repr<test::Relation> {
    using type = std::uint32_t;

    static type to(test::Relation r) {
        return static_cast<type>(r);
    }

    static test::Relation from(type v) {
        return static_cast<test::Relation>(v);
    }
};

template <>
struct repr<test::Version> {
    using type = std::string;

    static type to(const test::Version& v) {
        return std::format("{}.{}", v.major, v.minor);
    }

    static test::Version from(const std::string& encoded) {
        test::Version v;
        auto dot = encoded.find('.');
        std::from_chars(encoded.data(), encoded.data() + dot, v.major);
        std::from_chars(encoded.data() + dot + 1, encoded.data() + encoded.size(), v.minor);
        return v;
    }
};

template <>
struct repr<test::AuditStamp> {
    using type = std::uint64_t;

    static type to(const test::AuditStamp& s) {
        return s.at;
    }
};

template <>
struct repr<test::HexId> {
    using type = std::string;

    template <typename Config>
    static bool serialize(auto& vis, const test::HexId& h) {
        return vis.visit_str(std::format("{:08x}", h.v));
    }

    template <typename Config>
    static bool deserialize(auto& vis, test::HexId& h) {
        std::string text;
        if(!vis.visit_str(text)) {
            return false;
        }
        std::from_chars(text.data(), text.data() + text.size(), h.v, 16);
        return true;
    }
};

template <>
struct repr<test::DynamicBox> {
    using type = dynamic;

    template <typename Config>
    static bool serialize(auto& vis, const test::DynamicBox& box) {
        return std::visit([&](const auto& alt) { return codec::encode_value<Config>(vis, alt); },
                          box.v);
    }

    template <typename Config>
    static bool deserialize(auto& vis, test::DynamicBox& box) {
        if(vis.peek_kind() == type_kind::string) {
            std::string text;
            if(!vis.visit_str(text)) {
                return false;
            }
            box.v = std::move(text);
            return true;
        }
        std::int64_t n = 0;
        if(!vis.visit_int(n)) {
            return false;
        }
        box.v = n;
        return true;
    }
};

template <>
struct repr<test::StepId> {
    using type = std::uint32_t;

    static type to(test::StepId s) {
        return s.v;
    }

    static test::StepId from(type v) {
        return {.v = v};
    }
};

template <>
struct repr<test::Ticket> {
    using type = test::StepId;

    static type to(const test::Ticket& t) {
        return t.id;
    }

    static test::Ticket from(type id) {
        return {.id = id};
    }
};

template <>
struct repr<test::Lamport> {
    using type = std::optional<std::uint32_t>;

    static type to(const test::Lamport& s) {
        return s.tick == 0 ? type{} : type{s.tick};
    }

    static test::Lamport from(type v) {
        return {.tick = v.value_or(0)};
    }
};

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
struct repr<test::LineRange> {
    using type = annotate<test::StrictCamelTag>::type<test::LineSpan>;

    static type to(const test::LineRange& r) {
        return {
            {.start_line = r.first, .line_count = r.last - r.first}
        };
    }

    static test::LineRange from(const type& span) {
        const auto& s = annotated_value(span);
        return {.first = s.start_line, .last = s.start_line + s.line_count};
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
struct repr<test::BoxedScalar> {
    using type = std::variant<std::int8_t, double>;

    static type to(const test::BoxedScalar& b) {
        return b.v;
    }

    static test::BoxedScalar from(type v) {
        return {.v = v};
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
