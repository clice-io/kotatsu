#pragma once

// meta::repr fixtures the codec's tests use beyond tests/fixtures/repr.h:
// every form of the repr protocol, and the plain structs that describe the
// documents they encode to. DynamicBox writes through the codec itself.

#include <cctype>
#include <charconv>
#include <compare>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "fixtures/repr.h"
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
struct repr<test::BoxedScalar> {
    using type = std::variant<std::int8_t, double>;

    static type to(const test::BoxedScalar& b) {
        return b.v;
    }

    static test::BoxedScalar from(type v) {
        return {.v = v};
    }
};

}  // namespace kota::meta
