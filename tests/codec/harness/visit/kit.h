#pragma once

// The shared protocol suite. The visit layer (visit/encode.h, visit/decode.h,
// meta::repr, attrs, config) is one protocol shared by every backend, so its
// behaviour is written once, in the area headers next to this one, and each
// backend runs every area through a Kit over its adapter:
//
//     ZEST_CASE_GROUP(protocol) {
//         test::values(test::Kit<test::Json>{add_case});
//     }
//
// A case states its expectation independently of the format: a value equal
// after a roundtrip, a document equal to the one a plain struct (no
// attributes, fields named as the document names them) encodes to, or a
// failure's message and path. Differences between backends are Caps, chosen
// with `if constexpr`, so an excluded case is never instantiated.
//
// Each primitive below registers one case. Values come from factories,
// `[] { return ...; }`: a case body must be copyable, and some values are
// move-only. A plain value always encodes under the default config; Config
// applies to the value under test only.

#include <concepts>
#include <cstddef>
#include <expected>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

#include "fixtures/structs.h"
#include "kota/zest/zest.h"
#include "kota/meta/compare.h"
#include "kota/codec/visit/context.h"

namespace kota::test {

/// What a backend's documents can carry. Declared by the adapter rather than
/// derived from the library's traits, so the library does not judge itself.
struct Caps {
    /// Keyed documents (json, toml, dyn): fields travel by name, so alias,
    /// defaulted, deny_unknown_fields and required fields apply; tagged
    /// variants take object shapes and untagged ones decode by probing; a
    /// document written from one type can be read as another.
    bool self_describing = false;
    /// A field can be absent (key or slot), so skip_if omits it and decode
    /// keeps the default.
    bool absent_fields = false;
    /// uint64 above int64's maximum encodes.
    bool full_uint64 = false;
    /// A null below the root reads back as null: as a sequence element, a map
    /// value or a field's value.
    bool null_elements = false;
    /// NaN and infinities survive nan_repr::Passthrough.
    bool non_finite = false;
    /// Reflected structs as map keys.
    bool struct_keys = false;
    /// enum_repr::String and nan_repr::String compile.
    bool string_knobs = false;
    /// meta::dynamic reprs compile.
    bool dynamic_repr = false;
    /// Documents come from outside the program, so decoding garbage is in
    /// scope.
    bool untrusted_input = false;
    /// A format tag scopes meta::repr specializations to the backend.
    bool format_tag = false;
};

/// A backend adapter: its name and caps, its document type, encode and decode
/// under a Config, and a readable rendering of a document for reports and
/// snapshots.
template <typename B>
concept Backend = requires(const typename B::Encoded& encoded, int& out) {
    { B::name } -> std::convertible_to<std::string_view>;
    { B::caps } -> std::convertible_to<Caps>;
    { B::encode(0) } -> std::same_as<std::expected<typename B::Encoded, codec::rich_error>>;
    { B::decode(encoded, out) } -> std::same_as<std::expected<void, codec::rich_error>>;
    { B::render(encoded) } -> std::same_as<std::string>;
};

/// Where the cases of backend B are registered.
template <Backend B>
struct Kit {
    const zest::CaseRegistrar& add;
};

/// An expected failure. `message` is exact when the protocol layer words the
/// error; empty when the backend does, whose own tests pin the wording.
struct Failure {
    std::string_view message;
    std::string_view path;
};

/// `result` holds a value; a failed check shows the error.
template <typename T>
zest::Match succeeds(const std::expected<T, codec::rich_error>& result) {
    return zest::Match{.held = result.has_value(),
                       .explain = [&] { return result.error().to_string(); }};
}

namespace detail {

inline void check_failure(const codec::rich_error& error, Failure failure) {
    if(failure.message.empty()) {
        EXPECT(!error.message.empty());
    } else {
        EXPECT(error.message == failure.message);
    }
    EXPECT(error.format_path() == failure.path);
}

}  // namespace detail

/// make() survives encode and decode unchanged, and its decoded value encodes
/// to the same document.
template <typename Config = void, Backend B, typename Make>
void roundtrip(const Kit<B>& kit, std::string name, Make make) {
    kit.add(std::move(name), [make] {
        auto value = make();
        auto encoded = B::template encode<Config>(value);
        ASSERT(succeeds(encoded));
        ZEST_CONTEXT("{}: {}", B::name, B::render(*encoded));
        decltype(value) decoded{};
        ASSERT(succeeds(B::template decode<Config>(*encoded, decoded)));
        auto again = B::template encode<Config>(decoded);
        ASSERT(succeeds(again));
        EXPECT(*again == *encoded);
        EXPECT(meta::eq(decoded, value));
    });
}

/// make() under Config encodes to the document plain() encodes to.
template <typename Config = void, Backend B, typename Make, typename Plain>
void encodes_as(const Kit<B>& kit, std::string name, Make make, Plain plain) {
    kit.add(std::move(name), [make, plain] {
        auto expected = B::encode(plain());
        ASSERT(succeeds(expected));
        ZEST_CONTEXT("{}: {}", B::name, B::render(*expected));
        auto encoded = B::template encode<Config>(make());
        ASSERT(succeeds(encoded));
        EXPECT(*encoded == *expected);
    });
}

/// The document plain() encodes to decodes, under Config, into expect().
template <typename T, typename Config = void, Backend B, typename Plain, typename Expect>
void reads(const Kit<B>& kit, std::string name, Plain plain, Expect expect) {
    kit.add(std::move(name), [plain, expect] {
        auto document = B::encode(plain());
        ASSERT(succeeds(document));
        ZEST_CONTEXT("{}: {}", B::name, B::render(*document));
        T decoded{};
        ASSERT(succeeds(B::template decode<Config>(*document, decoded)));
        EXPECT(meta::eq(decoded, expect()));
    });
}

/// reads with the value in a field: the document of Field{plain()} decodes,
/// under Config, into Field<V>{expect()}. A backend that routes roots
/// differently (toml) then reads the value as every other backend does.
template <typename V, typename Config = void, Backend B, typename Plain, typename Expect>
void reads_in_field(const Kit<B>& kit, std::string name, Plain plain, Expect expect) {
    reads<Field<V>, Config>(
        kit,
        std::move(name),
        [plain] { return Field<decltype(plain())>{plain()}; },
        [expect] { return Field<V>{expect()}; });
}

/// The document plain() encodes to does not decode into T under Config.
template <typename T, typename Config = void, Backend B, typename Plain>
void read_fails(const Kit<B>& kit, std::string name, Plain plain, Failure failure) {
    kit.add(std::move(name), [plain, failure] {
        auto document = B::encode(plain());
        ASSERT(succeeds(document));
        ZEST_CONTEXT("{}: {}", B::name, B::render(*document));
        T decoded{};
        auto status = B::template decode<Config>(*document, decoded);
        ASSERT(!status);
        detail::check_failure(status.error(), failure);
    });
}

/// read_fails with the value in a field, as reads_in_field.
template <typename V, Backend B, typename Plain>
void read_in_field_fails(const Kit<B>& kit, std::string name, Plain plain, Failure failure) {
    read_fails<Field<V>>(
        kit,
        std::move(name),
        [plain] { return Field<decltype(plain())>{plain()}; },
        failure);
}

/// make() does not encode under Config.
template <typename Config = void, Backend B, typename Make>
void write_fails(const Kit<B>& kit, std::string name, Make make, Failure failure) {
    kit.add(std::move(name), [make, failure] {
        auto encoded = B::template encode<Config>(make());
        ASSERT(!encoded);
        detail::check_failure(encoded.error(), failure);
    });
}

/// The rendered document make() encodes to matches the case's snapshot.
template <Backend B, typename Make>
void snapshot(const Kit<B>& kit, std::string name, Make make) {
    kit.add(std::move(name), [make] {
        auto encoded = B::encode(make());
        ASSERT(succeeds(encoded));
        EXPECT_SNAPSHOT(B::render(*encoded));
    });
}

/// Every prefix of make()'s document, and the document with any one unit
/// changed, is rejected or decodes to a value whose document is stable: it
/// decodes and encodes again to itself. Overreads are the sanitizers' to
/// catch. For backends with untrusted_input, whose documents are byte or
/// character sequences.
template <Backend B, typename Make>
void hostile(const Kit<B>& kit, std::string name, Make make) {
    kit.add(std::move(name), [make] {
        using T = decltype(make());
        using Encoded = typename B::Encoded;
        using Unit = std::ranges::range_value_t<Encoded>;

        auto encoded = B::encode(make());
        ASSERT(succeeds(encoded));
        const Encoded& document = *encoded;

        auto settles = [](const Encoded& input) {
            T decoded{};
            if(!B::decode(input, decoded)) {
                return;
            }
            auto first = B::encode(decoded);
            ASSERT(succeeds(first));
            T again{};
            ASSERT(succeeds(B::decode(*first, again)));
            auto second = B::encode(again);
            ASSERT(succeeds(second));
            EXPECT(*second == *first);
        };
        // One failure is enough to show; the rest would repeat it.
        auto failed = [] {
            return zest::current_test_state() == zest::TestState::Failed;
        };

        for(std::size_t size = 0; size < document.size() && !failed(); ++size) {
            ZEST_CONTEXT("the first {} units", size);
            settles(Encoded(document.begin(), document.begin() + size));
        }
        // One mask moves a digit or a structural character to its neighbour,
        // the other sets the high bit.
        for(unsigned mask: {0x01U, 0x80U}) {
            for(std::size_t at = 0; at < document.size() && !failed(); ++at) {
                ZEST_CONTEXT("unit {} xor {:#04x}", at, mask);
                Encoded mutated = document;
                mutated[at] = static_cast<Unit>(static_cast<unsigned char>(mutated[at]) ^ mask);
                settles(mutated);
            }
        }
    });
}

}  // namespace kota::test
