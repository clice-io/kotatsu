#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "codec/fbs/harness/over_aligned.h"
#include "codec/fbs/harness/struct_keys.h"
#include "codec/harness/fixtures/attrs.h"
#include "codec/harness/fixtures/containers.h"
#include "codec/harness/fixtures/enums.h"
#include "codec/harness/fixtures/repr.h"
#include "codec/harness/fixtures/structs.h"
#include "fixtures/repr.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/meta/annotation.h"
#include "kotatsu/meta/attrs.h"
#include "kotatsu/codec/fbs/fbs.h"

// The zero-copy views over a verified buffer: table_view, its bound_view,
// and the array, map, tuple and variant views they hand out, their lookups,
// and how they follow reprs, behavior attrs and nullable wrappers.

namespace kotatsu::codec {

namespace {

using fbs::bound_view;
using fbs::table_view;
using test::Mesh;
using test::Vec4;

/// Whether the views address Member.
template <typename T, auto Member>
concept viewable = requires(const table_view<T>& view, const bound_view<T>& bound) {
    view[Member];
    view.has(Member);
    bound[Member];
};

struct Profile {
    std::int32_t id;
    std::string name;
    test::Point pos;
    std::vector<std::int32_t> scores;
    test::Address addr;
    test::SignedEnum sign;
    bool active;
    long double ratio;
    char initial;
    std::byte flag;
    std::set<std::int32_t> tags;
    std::vector<long double> samples;
};

auto make_profile() -> Profile {
    return {
        .id = 7,
        .name = "alice",
        .pos = {.x = 10, .y = 20},
        .scores = {1, 2, 3},
        .addr = {.city = "sh", .zip = 200000},
        .sign = test::SignedEnum::neg,
        .active = true,
        .ratio = 2.5L,
        .initial = 'k',
        .flag = std::byte{0x5A},
        .tags = {5, 9},
        .samples = {1.5L, -2.25L},
    };
}

struct NullableFields {
    std::optional<std::int32_t> number;
    std::optional<std::string> text;
    std::optional<test::Address> addr;
    std::unique_ptr<test::Address> owned;
    std::shared_ptr<test::Address> shared;
    std::weak_ptr<std::int32_t> watched;
};

/// A tagged variant whose type has a repr, in a table.
struct HoldsTaggedStamp {
    test::TaggedStampOrNote stamp;
    std::int32_t after = 0;
};

struct Choices {
    std::variant<std::int32_t, std::string> scalar;
    std::variant<std::monostate, test::Address> table;
    std::vector<std::variant<std::int32_t, std::string>> list;
};

struct TupleFields {
    std::pair<std::int32_t, std::string> pair;
    std::tuple<std::int32_t, std::string, double> triple;
    std::pair<std::string, test::Address> keyed;
    std::vector<std::pair<std::int32_t, std::string>> rows;
    std::tuple<std::int32_t, std::array<std::int32_t, 3>> nested;
};

struct Lists {
    std::vector<std::string> words;
    std::vector<test::Address> places;
    std::vector<test::Point> points;
    std::vector<std::optional<std::int32_t>> maybe;
    std::vector<std::vector<std::byte>> blobs;
    std::vector<std::vector<std::int32_t>> grid;
};

/// One column of each element type a contiguous array_view may hold, and
/// of some it may not.
struct Columns {
    std::vector<std::uint64_t> hashes;
    std::vector<std::uint32_t> ends;
    std::vector<std::int16_t> deltas;
    std::vector<float> weights;
    std::vector<double> scores;
    std::vector<std::uint8_t> postings;
    std::vector<char> letters;
    std::vector<std::byte> blob;
    std::vector<test::Point> points;
    std::vector<test::Point2d> corners;
    std::vector<Vec4> quads;
    std::vector<bool> flags;
    std::vector<test::SignedEnum> signs;
    std::vector<long double> wide;
};

auto make_columns() -> Columns {
    return {
        .hashes = {0xFFFFFFFFFFFFFFFFULL, 2, 3},
        .ends = {4, 9},
        .deltas = {-3, 7},
        .weights = {0.5F},
        .scores = {1.25, -8.0},
        .postings = {0xAB, 0xCD},
        .letters = {'k', static_cast<char>(0xE9)},
        .blob = {std::byte{0x01}, std::byte{0xFE}},
        .points = {{.x = 1, .y = 2}, {.x = 3, .y = 4}},
        .corners = {{.x = 0.5, .y = -1.5}},
        .quads = {{.x = 1, .y = 2, .z = 3, .w = 4}},
        .flags = {true, false, true},
        .signs = {test::SignedEnum::neg, test::SignedEnum::pos},
        .wide = {0.25L},
    };
}

/// Columns one level down: in a nested table and in a vector of tables, and
/// a column as the boxed elements of a vector of vectors.
struct ColumnTables {
    Columns nested;
    std::vector<Columns> rows;
    std::vector<std::vector<std::uint64_t>> grid;
};

/// Checks that a column reads as an empty range, handing out no pointer.
template <typename Column>
void expect_empty(std::string_view name, const Column& column) {
    ZEST_CONTEXT("{}", name);
    ZEXPECT(column.valid());
    ZEXPECT(column.empty());
    ZEXPECT(std::ranges::distance(column) == 0);
    if constexpr(std::ranges::contiguous_range<Column>) {
        ZEXPECT(column.data() == nullptr);
    }
}

/// Checks that every column of a Columns table reads as an empty range.
void expect_empty_columns(std::string_view table, const table_view<Columns>& columns) {
    ZEST_CONTEXT("{}", table);
    ZASSERT(columns.valid());
    expect_empty("hashes", columns[&Columns::hashes]);
    expect_empty("ends", columns[&Columns::ends]);
    expect_empty("deltas", columns[&Columns::deltas]);
    expect_empty("weights", columns[&Columns::weights]);
    expect_empty("scores", columns[&Columns::scores]);
    expect_empty("postings", columns[&Columns::postings]);
    expect_empty("letters", columns[&Columns::letters]);
    expect_empty("blob", columns[&Columns::blob]);
    expect_empty("points", columns[&Columns::points]);
    expect_empty("corners", columns[&Columns::corners]);
    expect_empty("quads", columns[&Columns::quads]);
    expect_empty("flags", columns[&Columns::flags]);
    expect_empty("signs", columns[&Columns::signs]);
    expect_empty("wide", columns[&Columns::wide]);
}

/// An unsigned key whose value a negative probe converts to.
struct UnsignedKeyed {
    std::map<std::uint32_t, std::int32_t> by_id;
};

struct MapFields {
    std::map<std::string, std::int32_t> by_name;
    std::map<std::int32_t, std::string> by_id;
    std::map<std::string, test::Address> places;
    std::unordered_map<test::SignedEnum, std::int32_t> by_sign;
    std::unordered_map<std::uint64_t, std::int32_t> by_wide_id;
    std::map<std::string, std::int32_t> none;
};

auto make_map_fields() -> MapFields {
    return {
        .by_name = {{"alpha", 1}, {"beta", 2}, {"gamma", 3}},
        .by_id = {{-7, "minus"}, {2, "two"}, {10, "ten"}, {30, "thirty"}},
        .places = {{"home", {.city = "sf", .zip = 94102}}, {"work", {.city = "la", .zip = 90001}}},
        .by_sign = {{test::SignedEnum::pos, 3},
                    {test::SignedEnum::neg, 1},
                    {test::SignedEnum::zero, 2}},
        .by_wide_id = {{2U, 1}, {0x8000000000000001ULL, 2}, {42U, 3}},
        .none = {},
    };
}

struct CharKeyed {
    std::map<char, std::int32_t> letters;
};

using test::OccurrenceKey;
using test::StructKeyed;

using Decimal = meta::annotation<int, meta::behavior::with<test::DecimalText>>;
using PackedVersion = meta::annotation<test::Version, meta::behavior::with<test::VersionAsNumber>>;

struct Reprs {
    test::Relation relation;
    test::HexId hex;
    test::Journal journal;
    std::vector<test::Journal> journals;
    std::vector<test::Lamport> stamps;
    std::map<test::Relation, std::int32_t> by_relation;
    std::map<std::string, test::Relation> relation_by_name;
    PackedVersion packed;
    std::optional<Decimal> maybe_decimal;
    meta::annotation<std::int32_t, meta::behavior::as<std::int64_t>> widened;
    test::AccessGrant grant;
};

ZEST_SUITE(codec_fbs_proxy) {

ZEST_CASE(table_view_reads_every_field_kind) {
    auto bytes = fbs::to_bytes(make_profile());
    ZASSERT(bytes);
    auto root = table_view<Profile>::from_bytes(*bytes);
    ZASSERT(root.valid());
    ZEXPECT(root[&Profile::id] == 7);
    ZEXPECT(root[&Profile::name] == "alice");
    // An inline struct reads as a copy, a table as a nested view.
    const test::Point pos = root[&Profile::pos];
    ZEXPECT(pos == test::Point{.x = 10, .y = 20});
    auto scores = root[&Profile::scores];
    ZASSERT(scores.size() == 3U);
    ZEXPECT(scores[2] == 3);
    auto addr = root[&Profile::addr];
    ZASSERT(addr.valid());
    ZEXPECT(addr[&test::Address::city] == "sh");
    ZEXPECT(addr[&test::Address::zip] == 200000);
    ZEXPECT(root[&Profile::sign] == test::SignedEnum::neg);
    ZEXPECT(root[&Profile::active]);
    ZEXPECT(root[&Profile::ratio] == 2.5L);
    ZEXPECT(root[&Profile::initial] == 'k');
    ZEXPECT(root[&Profile::flag] == std::byte{0x5A});
    auto tags = root[&Profile::tags];
    ZASSERT(tags.size() == 2U);
    ZEXPECT(tags[1] == 9);
    // A long double element is a double cell, read back as a long double.
    auto samples = root[&Profile::samples];
    ZASSERT(samples.size() == 2U);
    ZEXPECT(samples[1] == -2.25L);
}

ZEST_CASE(invalid_view_reads_defaults) {
    table_view<Profile> root;
    ZEXPECT(!root.valid());
    ZEXPECT(root[&Profile::id] == 0);
    ZEXPECT(root[&Profile::name] == "");
    ZEXPECT(!root[&Profile::addr].valid());
    ZEXPECT(root[&Profile::scores].size() == 0U);
    ZEXPECT(!root.has(&Profile::id));
}

ZEST_CASE(nullable_fields_peel_to_their_value) {
    auto owner = std::make_shared<std::int32_t>(77);
    NullableFields engaged{
        .number = 42,
        .text = "hello",
        .addr = test::Address{.city = "paris",  .zip = 75000},
        .owned = std::make_unique<test::Address>(test::Address{.city = "berlin", .zip = 10115}
          ),
        .shared = std::make_shared<test::Address>(test::Address{.city = "london", .zip = 20000}
          ),
        .watched = owner,
    };
    auto bytes = fbs::to_bytes(engaged);
    ZASSERT(bytes);
    auto root = table_view<NullableFields>::from_bytes(*bytes);
    ZASSERT(root.valid());
    ZEXPECT(root.has(&NullableFields::number));
    ZEXPECT(root[&NullableFields::number] == 42);
    ZEXPECT(root[&NullableFields::text] == "hello");
    ZEXPECT(root[&NullableFields::addr][&test::Address::city] == "paris");
    ZEXPECT(root[&NullableFields::owned][&test::Address::zip] == 10115);
    ZEXPECT(root[&NullableFields::shared][&test::Address::city] == "london");
    ZEXPECT(root[&NullableFields::watched] == 77);
}

ZEST_CASE(absent_nullable_fields_read_defaults) {
    // An expired weak_ptr writes nothing, like an empty optional.
    std::weak_ptr<std::int32_t> expired = std::make_shared<std::int32_t>(1);
    auto bytes = fbs::to_bytes(NullableFields{.number = std::nullopt,
                                              .text = std::nullopt,
                                              .addr = std::nullopt,
                                              .owned = nullptr,
                                              .shared = nullptr,
                                              .watched = expired});
    ZASSERT(bytes);
    auto root = table_view<NullableFields>::from_bytes(*bytes);
    ZASSERT(root.valid());
    ZEXPECT(!root.has(&NullableFields::number));
    ZEXPECT(root[&NullableFields::number] == 0);
    ZEXPECT(!root.has(&NullableFields::text));
    ZEXPECT(root[&NullableFields::text] == "");
    ZEXPECT(!root[&NullableFields::addr].valid());
    ZEXPECT(!root[&NullableFields::owned].valid());
    ZEXPECT(!root.has(&NullableFields::shared));
    ZEXPECT(root[&NullableFields::watched] == 0);
}

ZEST_CASE(skipped_member_has_no_slot) {
    // The skipped member is not in the buffer, and the members after it
    // read the slots after the one before it.
    auto bytes = fbs::to_bytes(test::FlattenInnerWithSkip{.keep_a = 3, .drop_b = 999, .keep_c = 5});
    ZASSERT(bytes);
    auto root = table_view<test::FlattenInnerWithSkip>::from_bytes(*bytes);
    ZASSERT(root.valid());
    ZEXPECT(root[&test::FlattenInnerWithSkip::keep_a] == 3);
    ZSTATIC_EXPECT(!viewable<test::FlattenInnerWithSkip, &test::FlattenInnerWithSkip::drop_b>);
    ZEXPECT(root[&test::FlattenInnerWithSkip::keep_c] == 5);
    ZEXPECT(root.has(&test::FlattenInnerWithSkip::keep_c));
    const bound_view bound(root);
    ZEXPECT(bound[&test::FlattenInnerWithSkip::keep_c] == 5);
}

ZEST_CASE(flattened_member_has_no_slot) {
    // A flattened member's own fields take the enclosing table's slots, so
    // the member itself is not addressable; the members after it read past
    // all of them, at any depth of flattening.
    ZSTATIC_EXPECT(!viewable<test::Outer, &test::Outer::inner>);
    ZSTATIC_EXPECT(!viewable<test::DeepOuter, &test::DeepOuter::mid>);

    auto outer = fbs::to_bytes(test::Outer{
        .x = 1,
        .inner = {{.a = 0x10000000, .b = 3}},
        .y = 4,
    });
    ZASSERT(outer);
    auto root = table_view<test::Outer>::from_bytes(*outer);
    ZASSERT(root.valid());
    ZEXPECT(root[&test::Outer::x] == 1);
    ZEXPECT(root[&test::Outer::y] == 4);
    const bound_view bound(root);
    ZEXPECT(bound[&test::Outer::x] == 1);
    ZEXPECT(bound[&test::Outer::y] == 4);

    auto deep = fbs::to_bytes(test::DeepOuter{
        .head = 5,
        .mid = {{.m = 6, .deep = {{.p = 7, .q = 8}}}},
        .tail = 9,
    });
    ZASSERT(deep);
    auto deep_root = table_view<test::DeepOuter>::from_bytes(*deep);
    ZASSERT(deep_root.valid());
    ZEXPECT(deep_root[&test::DeepOuter::head] == 5);
    ZEXPECT(deep_root[&test::DeepOuter::tail] == 9);
}

ZEST_CASE(non_member_is_not_viewable) {
    // Only Profile's own data members, by pointer.
    ZSTATIC_EXPECT(viewable<Profile, &Profile::id>);
    ZSTATIC_EXPECT(!viewable<Profile, &test::Address::zip>);
    ZSTATIC_EXPECT(!viewable<Profile, 0>);
}

ZEST_CASE(bound_view_reads_what_the_table_view_reads) {
    auto bytes = fbs::to_bytes(make_profile());
    ZASSERT(bytes);
    auto root = table_view<Profile>::from_bytes(*bytes);
    ZASSERT(root.valid());
    const bound_view bound(root);
    ZEXPECT(bound[&Profile::id] == 7);
    ZEXPECT(bound[&Profile::name] == "alice");
    ZEXPECT(bound[&Profile::pos] == root[&Profile::pos]);
    ZEXPECT(bound[&Profile::addr][&test::Address::city] == "sh");
    ZEXPECT(bound[&Profile::sign] == test::SignedEnum::neg);
    ZEXPECT(bound[&Profile::initial] == 'k');
    ZEXPECT(bound[&Profile::flag] == std::byte{0x5A});
    ZEXPECT(bound[&Profile::ratio] == 2.5L);
    // A sequence binds its pointer and length once.
    const auto& scores = bound[&Profile::scores];
    ZEXPECT(scores.data() == root[&Profile::scores].data());
    ZEXPECT(std::span<const std::int32_t>(scores) == std::vector<std::int32_t>{1, 2, 3});
    ZEXPECT(bound[&Profile::samples][1] == -2.25L);
}

ZEST_CASE(bound_view_of_an_invalid_view_reads_defaults) {
    const bound_view bound{table_view<Profile>()};
    ZEXPECT(bound[&Profile::id] == 0);
    ZEXPECT(bound[&Profile::name] == "");
    ZEXPECT(!bound[&Profile::addr].valid());
    ZEXPECT(!bound[&Profile::scores].valid());
    ZEXPECT(bound[&Profile::scores].empty());
}

ZEST_CASE(variant_view_reads_the_held_alternative) {
    auto bytes = fbs::to_bytes(Choices{
        .scalar = std::string("kotatsu"),
        .table = test::Address{.city = "rome",  .zip = 100          },
        .list = {std::int32_t{7}, std::string("seven")},
    });
    ZASSERT(bytes);
    auto root = table_view<Choices>::from_bytes(*bytes);
    ZASSERT(root.valid());
    auto scalar = root[&Choices::scalar];
    ZEXPECT(scalar.index() == 1U);
    ZEXPECT(scalar.get<1>() == "kotatsu");
    auto table = root[&Choices::table];
    ZEXPECT(table.index() == 1U);
    ZEXPECT(table.get<1>()[&test::Address::city] == "rome");
    auto list = root[&Choices::list];
    ZASSERT(list.size() == 2U);
    ZEXPECT(list[0].index() == 0U);
    ZEXPECT(list[0].get<0>() == 7);
    ZEXPECT(list[1].get<1>() == "seven");
}

ZEST_CASE(tagged_variant_with_a_repr_reads_as_the_variant) {
    // The tag wins over the variant type's repr (a text): the field is a
    // variant table, which the verifier and the view read as one.
    auto stamped = fbs::to_bytes(HoldsTaggedStamp{
        .stamp = test::TaggedStampOrNote{test::Stamp{.n = 4}},
        .after = 9,
    });
    ZASSERT(stamped);
    auto root = table_view<HoldsTaggedStamp>::from_bytes(*stamped);
    ZASSERT(root.valid());
    auto stamp = root[&HoldsTaggedStamp::stamp];
    ZEXPECT(stamp.index() == 0U);
    ZEXPECT(stamp.get<0>().n == 4);
    ZEXPECT(root[&HoldsTaggedStamp::after] == 9);

    auto noted = fbs::to_bytes(HoldsTaggedStamp{
        .stamp = test::TaggedStampOrNote{std::string("memo")},
        .after = 1,
    });
    ZASSERT(noted);
    auto note = table_view<HoldsTaggedStamp>::from_bytes(*noted)[&HoldsTaggedStamp::stamp];
    ZEXPECT(note.index() == 1U);
    ZEXPECT(note.get<1>() == "memo");
}

ZEST_CASE(variant_view_reads_a_monostate_alternative) {
    auto bytes = fbs::to_bytes(Choices{.scalar = 0, .table = std::monostate{}, .list = {}});
    ZASSERT(bytes);
    auto root = table_view<Choices>::from_bytes(*bytes);
    ZASSERT(root.valid());
    auto table = root[&Choices::table];
    ZASSERT(table.valid());
    ZEXPECT(table.index() == 0U);
    // A null alternative reads as its value, without reading its slot.
    ZEXPECT(table.get<0>() == std::monostate{});
}

ZEST_CASE(tuple_view_reads_elements) {
    auto bytes = fbs::to_bytes(TupleFields{
        .pair = {42, "hello"},
        .triple = {7, "world", 3.14},
        .keyed = {"key", {.city = "nyc", .zip = 10001}},
        .rows = {{1, "a"}, {3, "c"}},
        .nested = {11, {21, 22, 23}},
    });
    ZASSERT(bytes);
    auto root = table_view<TupleFields>::from_bytes(*bytes);
    ZASSERT(root.valid());
    ZEXPECT(root[&TupleFields::pair].get<0>() == 42);
    ZEXPECT(root[&TupleFields::pair].get<1>() == "hello");
    ZEXPECT(root[&TupleFields::triple].get<2>() == 3.14);
    ZEXPECT(root[&TupleFields::keyed].get<1>()[&test::Address::zip] == 10001);
    auto rows = root[&TupleFields::rows];
    ZASSERT(rows.size() == 2U);
    ZEXPECT(rows[1].get<1>() == "c");
    // std::array is tuple-like: a tuple_view too.
    ZEXPECT(root[&TupleFields::nested].get<1>().get<2>() == 23);
}

ZEST_CASE(array_view_reads_each_element_layout) {
    auto bytes = fbs::to_bytes(Lists{
        .words = {"hello", "world"},
        .places = {{.city = "a", .zip = 1}, {.city = "b", .zip = 2}},
        .points = {{.x = 1, .y = 2}, {.x = 3, .y = 4}},
        .maybe = {5, std::nullopt, 9},
        .blobs = {{std::byte{0xAA}, std::byte{0xBB}}, {}},
        .grid = {{1, 2}, {3}},
    });
    ZASSERT(bytes);
    auto root = table_view<Lists>::from_bytes(*bytes);
    ZASSERT(root.valid());
    ZEXPECT(root[&Lists::words][1] == "world");
    ZEXPECT(root[&Lists::places][1][&test::Address::city] == "b");
    ZEXPECT(root[&Lists::points][1].y == 4);
    // Boxed elements read through their table; an absent one as its default.
    auto maybe = root[&Lists::maybe];
    ZASSERT(maybe.size() == 3U);
    ZEXPECT(maybe[0] == 5);
    ZEXPECT(maybe[1] == 0);
    ZEXPECT(maybe[2] == 9);
    auto blobs = root[&Lists::blobs];
    ZASSERT(blobs.size() == 2U);
    ZEXPECT(blobs[0][1] == std::byte{0xBB});
    ZEXPECT(blobs[1].size() == 0U);
    ZEXPECT(root[&Lists::grid][0][1] == 2);
}

ZEST_CASE(array_view_is_contiguous_when_elements_are_stored_as_themselves) {
    using fbs::array_view;
    // Integers, float and double, char, std::byte and inline structs are
    // stored as themselves; bool, enum and long double elements are cells
    // converted on each read.
    ZSTATIC_EXPECT(std::ranges::contiguous_range<array_view<std::uint64_t>>);
    ZSTATIC_EXPECT(std::ranges::contiguous_range<array_view<std::int16_t>>);
    ZSTATIC_EXPECT(std::ranges::contiguous_range<array_view<float>>);
    ZSTATIC_EXPECT(std::ranges::contiguous_range<array_view<double>>);
    ZSTATIC_EXPECT(std::ranges::contiguous_range<array_view<char>>);
    ZSTATIC_EXPECT(std::ranges::contiguous_range<array_view<std::byte>>);
    ZSTATIC_EXPECT(std::ranges::contiguous_range<array_view<test::Point>>);
    ZSTATIC_EXPECT(std::ranges::contiguous_range<array_view<test::Point2d>>);
    ZSTATIC_EXPECT(std::ranges::contiguous_range<array_view<Vec4>>);
    // Aligned past operator new: copied out, wherever the buffer sits.
    ZSTATIC_EXPECT(!std::ranges::contiguous_range<array_view<test::Vec8>>);
    ZSTATIC_EXPECT(std::ranges::random_access_range<array_view<test::Vec8>>);
    ZSTATIC_EXPECT(!std::ranges::contiguous_range<array_view<bool>>);
    ZSTATIC_EXPECT(!std::ranges::contiguous_range<array_view<test::SignedEnum>>);
    ZSTATIC_EXPECT(!std::ranges::contiguous_range<array_view<long double>>);
    ZSTATIC_EXPECT(std::ranges::sized_range<array_view<std::uint32_t>>);
    ZSTATIC_EXPECT(std::ranges::borrowed_range<array_view<std::uint32_t>>);

    auto bytes = fbs::to_bytes(make_columns());
    ZASSERT(bytes);
    auto root = table_view<Columns>::from_bytes(*bytes);
    ZASSERT(root.valid());
    // A span builds straight from the view, over the buffer itself.
    const std::span<const std::uint64_t> hashes = root[&Columns::hashes];
    ZEXPECT(hashes == make_columns().hashes);
    const auto* begin = bytes->data();
    const auto* at = reinterpret_cast<const std::uint8_t*>(hashes.data());
    ZEXPECT(at > begin);
    ZEXPECT(at < begin + bytes->size());
    ZEXPECT(std::span<const std::uint32_t>(root[&Columns::ends]) == make_columns().ends);
    ZEXPECT(std::span<const std::int16_t>(root[&Columns::deltas]) == make_columns().deltas);
    ZEXPECT(std::span<const float>(root[&Columns::weights]) == make_columns().weights);
    ZEXPECT(std::span<const double>(root[&Columns::scores]) == make_columns().scores);
    ZEXPECT(std::span<const std::uint8_t>(root[&Columns::postings]) == make_columns().postings);
    ZEXPECT(std::span<const char>(root[&Columns::letters]) == make_columns().letters);
    ZEXPECT(std::span<const std::byte>(root[&Columns::blob]) == make_columns().blob);
    ZEXPECT(std::span<const test::Point>(root[&Columns::points]) == make_columns().points);
    ZEXPECT(std::span<const test::Point2d>(root[&Columns::corners]) == make_columns().corners);
    ZEXPECT(std::span<const Vec4>(root[&Columns::quads]) == make_columns().quads);
}

ZEST_CASE(empty_columns_read_as_empty_ranges) {
    // flatbuffers aligns a vector's data only when it has some, so an empty
    // vector's sits wherever its length word ends, which may be off a wide
    // element's alignment. With no element to read in place, it verifies
    // wherever it sits, and the view hands out no pointer to it.
    auto columns = fbs::to_bytes(Columns{});
    ZASSERT(columns);
    const auto root = table_view<Columns>::from_bytes(*columns);
    ZASSERT(root.valid());
    // The empty columns' length words sit 4 bytes apart, so some wide one's
    // data is off its alignment.
    const auto misaligned = [](const auto& column, std::size_t alignment) -> bool {
        return reinterpret_cast<std::uintptr_t>(column.raw()->Data()) % alignment != 0U;
    };
    ZASSERT((misaligned(root[&Columns::hashes], alignof(std::uint64_t)) ||
             misaligned(root[&Columns::corners], alignof(test::Point2d)) ||
             misaligned(root[&Columns::quads], alignof(Vec4))));
    expect_empty_columns("root", root);
    const bound_view bound(root);
    ZEXPECT(bound[&Columns::hashes].data() == nullptr);
    ZEXPECT(bound[&Columns::quads].data() == nullptr);

    auto tables = fbs::to_bytes(ColumnTables{
        .nested = {},
        .rows = {Columns{}, Columns{}},
        .grid = {{}, {}},
    });
    ZASSERT(tables);
    const auto tables_root = table_view<ColumnTables>::from_bytes(*tables);
    ZASSERT(tables_root.valid());
    expect_empty_columns("nested", tables_root[&ColumnTables::nested]);
    auto rows = tables_root[&ColumnTables::rows];
    ZASSERT(rows.size() == 2U);
    for(std::size_t i = 0; i < rows.size(); ++i) {
        expect_empty_columns(std::format("rows[{}]", i), rows[i]);
    }
    auto grid = tables_root[&ColumnTables::grid];
    ZASSERT(grid.size() == 2U);
    for(std::size_t i = 0; i < grid.size(); ++i) {
        expect_empty(std::format("grid[{}]", i), grid[i]);
    }
}

ZEST_CASE(array_view_iterates_every_element_layout) {
    // Elements not stored as themselves read one by one, through a
    // random-access iterator.
    using fbs::array_view;
    ZSTATIC_EXPECT(std::ranges::random_access_range<array_view<bool>>);
    ZSTATIC_EXPECT(std::ranges::random_access_range<array_view<std::string>>);
    ZSTATIC_EXPECT(std::ranges::random_access_range<array_view<test::Address>>);
    ZSTATIC_EXPECT(std::ranges::random_access_range<array_view<std::optional<std::int32_t>>>);

    auto columns = fbs::to_bytes(make_columns());
    ZASSERT(columns);
    auto root = table_view<Columns>::from_bytes(*columns);
    ZASSERT(root.valid());
    auto flags = root[&Columns::flags];
    ZEXPECT(std::vector<bool>(flags.begin(), flags.end()) == make_columns().flags);
    auto signs = root[&Columns::signs];
    ZEXPECT(std::vector<test::SignedEnum>(signs.begin(), signs.end()) == make_columns().signs);
    auto wide = root[&Columns::wide];
    ZASSERT(std::ranges::distance(wide) == 1);
    ZEXPECT(*wide.begin() == 0.25L);

    auto lists = fbs::to_bytes(Lists{
        .words = {"hello", "world"},
        .places = {{.city = "a", .zip = 1}, {.city = "b", .zip = 2}},
        .points = {},
        .maybe = {5, std::nullopt, 9},
        .blobs = {},
        .grid = {},
    });
    ZASSERT(lists);
    auto lists_root = table_view<Lists>::from_bytes(*lists);
    ZASSERT(lists_root.valid());
    std::vector<std::string_view> words;
    for(auto word: lists_root[&Lists::words]) {
        words.push_back(word);
    }
    ZEXPECT(words == std::vector<std::string_view>{"hello", "world"});
    auto places = lists_root[&Lists::places];
    ZASSERT(places.size() == 2U);
    auto second = places.begin() + 1;
    ZEXPECT((*second)[&test::Address::city] == "b");
    ZEXPECT(second[-1][&test::Address::zip] == 1);
    ZEXPECT(places.end() - places.begin() == 2);
    ZEXPECT(std::ranges::count(lists_root[&Lists::maybe], 0) == 1);
}

ZEST_CASE(over_aligned_inline_struct_vector_is_contiguous) {
    // to_bytes aligns the elements for the struct; held at an address
    // aligned like it, the view hands them out in place.
    ZSTATIC_EXPECT(fbs::can_inline_struct_v<Vec4>);
    const Mesh mesh{
        .points = {{.x = 1, .y = 2, .z = 3, .w = 4}, {.x = 5, .y = 6, .z = 7, .w = 8}}
    };
    auto bytes = fbs::to_bytes(mesh);
    ZASSERT(bytes);
    auto root = table_view<Mesh>::from_bytes(*bytes);
    ZASSERT(root.valid());
    auto points = root[&Mesh::points];
    ZEXPECT(reinterpret_cast<std::uintptr_t>(points.data()) % alignof(Vec4) == 0U);
    ZEXPECT(std::span<const Vec4>(points) == mesh.points);
}

ZEST_CASE(array_view_out_of_range_reads_default) {
    auto bytes = fbs::to_bytes(make_profile());
    ZASSERT(bytes);
    auto scores = table_view<Profile>::from_bytes(*bytes)[&Profile::scores];
    ZASSERT(scores.size() == 3U);
    ZEXPECT(scores[3] == 0);
    ZEXPECT(scores[100] == 0);
}

ZEST_CASE(map_view_reads_entries_in_key_order) {
    auto bytes = fbs::to_bytes(make_map_fields());
    ZASSERT(bytes);
    auto root = table_view<MapFields>::from_bytes(*bytes);
    ZASSERT(root.valid());
    auto by_name = root[&MapFields::by_name];
    ZASSERT(by_name.size() == 3U);
    ZEXPECT(by_name.at(0).get<0>() == "alpha");
    ZEXPECT(by_name.at(2).get<1>() == 3);
    auto places = root[&MapFields::places];
    ZASSERT(places.size() == 2U);
    ZEXPECT(places.at(1).get<0>() == "work");
    ZEXPECT(places.at(1).get<1>()[&test::Address::zip] == 90001);
    ZEXPECT(root[&MapFields::none].empty());
    ZEXPECT(!by_name.at(3).valid());
}

ZEST_CASE(map_view_looks_up_string_keys) {
    auto bytes = fbs::to_bytes(make_map_fields());
    ZASSERT(bytes);
    auto by_name = table_view<MapFields>::from_bytes(*bytes)[&MapFields::by_name];
    ZASSERT(by_name.valid());
    ZEXPECT(by_name[std::string("beta")] == 2);
    ZEXPECT(by_name[std::string("missing")] == 0);
    auto found = by_name.find(std::string("gamma"));
    ZASSERT(found);
    ZEXPECT(found->get<0>() == "gamma");
    ZEXPECT(found->get<1>() == 3);
    ZEXPECT(!by_name.find(std::string("missing")));
    ZEXPECT(by_name.contains(std::string("alpha")));
    ZEXPECT(!by_name.contains(std::string("missing")));
    // A table value reads as a nested view; a missing one as an invalid view.
    auto places = table_view<MapFields>::from_bytes(*bytes)[&MapFields::places];
    ZASSERT(places.valid());
    ZEXPECT(places["work"][&test::Address::city] == "la");
    ZEXPECT(!places["nowhere"].valid());
}

ZEST_CASE(map_view_looks_up_transparently) {
    auto bytes = fbs::to_bytes(make_map_fields());
    ZASSERT(bytes);
    auto by_name = table_view<MapFields>::from_bytes(*bytes)[&MapFields::by_name];
    ZASSERT(by_name.valid());
    ZEXPECT(by_name["beta"] == 2);
    ZEXPECT(by_name.contains("alpha"));
    ZEXPECT(!by_name.contains("missing"));
    ZEXPECT(by_name[std::string_view("gamma")] == 3);
    auto found = by_name.find("beta");
    ZASSERT(found);
    ZEXPECT(found->get<1>() == 2);
    ZEXPECT(!by_name.find("nope"));
}

ZEST_CASE(map_view_looks_up_integer_and_enum_keys) {
    // The binary search compares numbers as numbers (2 before 10), unsigned
    // keys as unsigned, and enums by their underlying value, as the encoder
    // sorted them.
    auto bytes = fbs::to_bytes(make_map_fields());
    ZASSERT(bytes);
    auto root = table_view<MapFields>::from_bytes(*bytes);
    ZASSERT(root.valid());
    auto by_id = root[&MapFields::by_id];
    ZEXPECT(by_id[-7] == "minus");
    ZEXPECT(by_id[2] == "two");
    ZEXPECT(by_id[10] == "ten");
    ZEXPECT(by_id[30] == "thirty");
    ZEXPECT(by_id[99] == "");
    auto by_sign = root[&MapFields::by_sign];
    ZEXPECT(by_sign[test::SignedEnum::neg] == 1);
    ZEXPECT(by_sign[test::SignedEnum::pos] == 3);
    auto by_wide_id = root[&MapFields::by_wide_id];
    ZEXPECT(by_wide_id[42U] == 3);
    ZEXPECT(by_wide_id[0x8000000000000001ULL] == 2);
}

ZEST_CASE(map_view_looks_up_across_signedness) {
    // Integer keys and probes compare by value whatever their signedness.
    auto bytes = fbs::to_bytes(make_map_fields());
    ZASSERT(bytes);
    auto root = table_view<MapFields>::from_bytes(*bytes);
    ZASSERT(root.valid());
    auto by_id = root[&MapFields::by_id];
    ZEXPECT(by_id.contains(2U));
    ZEXPECT(by_id[30U] == "thirty");
    ZEXPECT(!by_id.contains(0xFFFFFFF9U));
    auto by_wide_id = root[&MapFields::by_wide_id];
    ZEXPECT(by_wide_id[42] == 3);

    // -1 converts to the one key the map holds.
    auto unsigned_bytes = fbs::to_bytes(UnsignedKeyed{.by_id = {{0xFFFFFFFFU, 1}}});
    ZASSERT(unsigned_bytes);
    auto unsigned_ids =
        table_view<UnsignedKeyed>::from_bytes(*unsigned_bytes)[&UnsignedKeyed::by_id];
    ZEXPECT(unsigned_ids.contains(0xFFFFFFFFU));
    ZEXPECT(!unsigned_ids.contains(-1));
    ZEXPECT(unsigned_ids[-1] == 0);
}

ZEST_CASE(map_view_orders_char_keys_as_int8) {
    // A char key sorts as the int8 cell it is stored as, so a buffer reads
    // the same where char is signed and where it is unsigned.
    const std::map<char, std::int32_t> letters{
        {'a',                     1},
        {static_cast<char>(0xE9), 2},
    };
    auto bytes = fbs::to_bytes(CharKeyed{.letters = letters});
    ZASSERT(bytes);
    auto map = table_view<CharKeyed>::from_bytes(*bytes)[&CharKeyed::letters];
    ZASSERT(map.size() == 2U);
    ZEXPECT(map.at(0).get<0>() == static_cast<char>(0xE9));
    ZEXPECT(map.at(1).get<0>() == 'a');
    ZEXPECT(map['a'] == 1);
    ZEXPECT(map[static_cast<char>(0xE9)] == 2);
}

ZEST_CASE(map_view_looks_up_struct_keys) {
    const std::array<OccurrenceKey, 3> keys{
        {
         {.range = {.begin = 1, .end = 0}, .target = 9, .weight = -2},
         {.range = {.begin = 1, .end = 0}, .target = 9, .weight = 3},
         {.range = {.begin = 2, .end = 0}, .target = 0, .weight = 0},
         }
    };
    StructKeyed input;
    for(std::size_t i = 0; i < keys.size(); ++i) {
        input.hits.emplace(keys[i], static_cast<std::int32_t>(i + 1));
    }
    auto bytes = fbs::to_bytes(input);
    ZASSERT(bytes);
    auto hits = table_view<StructKeyed>::from_bytes(*bytes)[&StructKeyed::hits];
    ZASSERT(hits.size() == 3U);
    ZEXPECT(hits[keys[0]] == 1);
    ZEXPECT(hits[keys[1]] == 2);
    ZEXPECT(hits[keys[2]] == 3);
    auto found = hits.find(keys[1]);
    ZASSERT(found);
    ZEXPECT(found->get<0>() == keys[1]);
    // Misses before the first entry, between two, and after the last.
    const OccurrenceKey between{
        .range = {.begin = 1, .end = 0},
        .target = 9,
        .weight = 0
    };
    ZEXPECT(!hits.contains(OccurrenceKey{
        .range = {.begin = 0, .end = 0},
        .target = 0,
        .weight = 0
    }));
    ZEXPECT(!hits.contains(between));
    ZEXPECT(!hits.contains(OccurrenceKey{
        .range = {.begin = 9, .end = 0},
        .target = 0,
        .weight = 0
    }));
    ZEXPECT(!hits.find(between));

    auto empty = fbs::to_bytes(StructKeyed{});
    ZASSERT(empty);
    ZEXPECT(!table_view<StructKeyed>::from_bytes(*empty)[&StructKeyed::hits].contains(keys[0]));
}

ZEST_CASE(views_read_what_reprs_and_attrs_carry) {
    // A view reads the representation the buffer holds: the repr's, a
    // format-scoped repr's, a field adapter's over the type's own repr, an
    // adapter's inside an optional, and as<> and enum_string rerouting.
    Reprs input{
        .relation = test::Relation::references,
        .hex = {.v = 0xBEEF},
        .journal = {.page = 41},
        .journals = {{.page = 3}, {.page = 9}},
        .stamps = {{.tick = 7}, {.tick = 0}},
        .by_relation = {{test::Relation::defines, 10}, {test::Relation::references, 20}},
        .relation_by_name = {{"a", test::Relation::declares}, {"b", test::Relation::references}},
        .packed = {{.major = 3, .minor = 14}},
        .maybe_decimal = Decimal{12},
        .widened = 1234,
        .grant = {.level = test::Access::full_control, .count = 2},
    };
    auto bytes = fbs::to_bytes(input);
    ZASSERT(bytes);
    auto root = table_view<Reprs>::from_bytes(*bytes);
    ZASSERT(root.valid());
    ZEXPECT(root[&Reprs::relation] == 102U);
    ZEXPECT(root[&Reprs::hex] == "0000beef");
    ZEXPECT(root[&Reprs::journal] == 41);
    ZEXPECT(root[&Reprs::journals][1] == 9);
    // A null repr is boxed; absent, it reads as the inner default.
    ZEXPECT(root[&Reprs::stamps][0] == 7U);
    ZEXPECT(root[&Reprs::stamps][1] == 0U);
    ZEXPECT(root[&Reprs::by_relation][102U] == 20);
    ZEXPECT(root[&Reprs::relation_by_name]["b"] == 102U);
    ZEXPECT(root[&Reprs::packed] == 3014U);
    ZEXPECT(root[&Reprs::maybe_decimal] == "12");
    ZEXPECT(root[&Reprs::widened] == 1234);
    ZEXPECT(root[&Reprs::grant][&test::AccessGrant::level] == "fullControl");
}

ZEST_CASE(from_verified_bytes_wraps_without_verifying) {
    // Verify once when a blob is loaded, then build views per query.
    auto bytes = fbs::to_bytes(make_profile());
    ZASSERT(bytes);
    ZASSERT(table_view<Profile>::from_bytes(*bytes).valid());
    auto root = table_view<Profile>::from_verified_bytes(*bytes);
    ZASSERT(root.valid());
    ZEXPECT(root[&Profile::name] == "alice");
    auto as_bytes = std::as_bytes(std::span(*bytes));
    ZEXPECT(table_view<Profile>::from_verified_bytes(as_bytes)[&Profile::id] == 7);
}

};  // ZEST_SUITE(codec_fbs_proxy)

}  // namespace

}  // namespace kotatsu::codec
