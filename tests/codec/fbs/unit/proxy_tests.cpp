#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "codec/fbs/harness/struct_keys.h"
#include "codec/harness/fixtures/attrs.h"
#include "codec/harness/fixtures/containers.h"
#include "codec/harness/fixtures/enums.h"
#include "codec/harness/fixtures/repr.h"
#include "codec/harness/fixtures/structs.h"
#include "fixtures/repr.h"
#include "kota/zest/zest.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"
#include "kota/codec/fbs/fbs.h"

// The zero-copy views over a verified buffer: table_view and the array,
// map, tuple and variant views it hands out, their lookups, and how they
// follow reprs, behavior attrs and nullable wrappers.

namespace kota::codec {

namespace {

using fbs::table_view;

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
    ASSERT(bytes);
    auto root = table_view<Profile>::from_bytes(*bytes);
    ASSERT(root.valid());
    EXPECT(root[&Profile::id] == 7);
    EXPECT(root[&Profile::name] == "alice");
    // An inline struct reads as a copy, a table as a nested view.
    const test::Point pos = root[&Profile::pos];
    EXPECT(pos == test::Point{.x = 10, .y = 20});
    auto scores = root[&Profile::scores];
    ASSERT(scores.size() == 3U);
    EXPECT(scores[2] == 3);
    auto addr = root[&Profile::addr];
    ASSERT(addr.valid());
    EXPECT(addr[&test::Address::city] == "sh");
    EXPECT(addr[&test::Address::zip] == 200000);
    EXPECT(root[&Profile::sign] == test::SignedEnum::neg);
    EXPECT(root[&Profile::active]);
    EXPECT(root[&Profile::ratio] == 2.5L);
    EXPECT(root[&Profile::initial] == 'k');
    EXPECT(root[&Profile::flag] == std::byte{0x5A});
    auto tags = root[&Profile::tags];
    ASSERT(tags.size() == 2U);
    EXPECT(tags[1] == 9);
    // A long double element is a double cell, read back as a long double.
    auto samples = root[&Profile::samples];
    ASSERT(samples.size() == 2U);
    EXPECT(samples[1] == -2.25L);
}

ZEST_CASE(invalid_view_reads_defaults) {
    table_view<Profile> root;
    EXPECT(!root.valid());
    EXPECT(root[&Profile::id] == 0);
    EXPECT(root[&Profile::name] == "");
    EXPECT(!root[&Profile::addr].valid());
    EXPECT(root[&Profile::scores].size() == 0U);
    EXPECT(!root.has(&Profile::id));
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
    ASSERT(bytes);
    auto root = table_view<NullableFields>::from_bytes(*bytes);
    ASSERT(root.valid());
    EXPECT(root.has(&NullableFields::number));
    EXPECT(root[&NullableFields::number] == 42);
    EXPECT(root[&NullableFields::text] == "hello");
    EXPECT(root[&NullableFields::addr][&test::Address::city] == "paris");
    EXPECT(root[&NullableFields::owned][&test::Address::zip] == 10115);
    EXPECT(root[&NullableFields::shared][&test::Address::city] == "london");
    EXPECT(root[&NullableFields::watched] == 77);
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
    ASSERT(bytes);
    auto root = table_view<NullableFields>::from_bytes(*bytes);
    ASSERT(root.valid());
    EXPECT(!root.has(&NullableFields::number));
    EXPECT(root[&NullableFields::number] == 0);
    EXPECT(!root.has(&NullableFields::text));
    EXPECT(root[&NullableFields::text] == "");
    EXPECT(!root[&NullableFields::addr].valid());
    EXPECT(!root[&NullableFields::owned].valid());
    EXPECT(!root.has(&NullableFields::shared));
    EXPECT(root[&NullableFields::watched] == 0);
}

ZEST_CASE(skipped_member_has_no_slot) {
    auto bytes = fbs::to_bytes(test::FlattenInnerWithSkip{.keep_a = 3, .drop_b = 999, .keep_c = 5});
    ASSERT(bytes);
    auto root = table_view<test::FlattenInnerWithSkip>::from_bytes(*bytes);
    ASSERT(root.valid());
    EXPECT(root[&test::FlattenInnerWithSkip::keep_a] == 3);
    EXPECT(!root.has(&test::FlattenInnerWithSkip::drop_b));
    EXPECT(root[&test::FlattenInnerWithSkip::keep_c] == 5);
}

ZEST_CASE(variant_view_reads_the_held_alternative) {
    auto bytes = fbs::to_bytes(Choices{
        .scalar = std::string("kotatsu"),
        .table = test::Address{.city = "rome",  .zip = 100          },
        .list = {std::int32_t{7}, std::string("seven")},
    });
    ASSERT(bytes);
    auto root = table_view<Choices>::from_bytes(*bytes);
    ASSERT(root.valid());
    auto scalar = root[&Choices::scalar];
    EXPECT(scalar.index() == 1U);
    EXPECT(scalar.get<1>() == "kotatsu");
    auto table = root[&Choices::table];
    EXPECT(table.index() == 1U);
    EXPECT(table.get<1>()[&test::Address::city] == "rome");
    auto list = root[&Choices::list];
    ASSERT(list.size() == 2U);
    EXPECT(list[0].index() == 0U);
    EXPECT(list[0].get<0>() == 7);
    EXPECT(list[1].get<1>() == "seven");
}

ZEST_CASE(variant_view_reads_a_monostate_alternative) {
    auto bytes = fbs::to_bytes(Choices{.scalar = 0, .table = std::monostate{}, .list = {}});
    ASSERT(bytes);
    auto root = table_view<Choices>::from_bytes(*bytes);
    ASSERT(root.valid());
    auto table = root[&Choices::table];
    ASSERT(table.valid());
    EXPECT(table.index() == 0U);
    // The alternative reads as the empty inline struct it is.
    EXPECT(table.get<0>() == std::monostate{});
}

ZEST_CASE(tuple_view_reads_elements) {
    auto bytes = fbs::to_bytes(TupleFields{
        .pair = {42, "hello"},
        .triple = {7, "world", 3.14},
        .keyed = {"key", {.city = "nyc", .zip = 10001}},
        .rows = {{1, "a"}, {3, "c"}},
        .nested = {11, {21, 22, 23}},
    });
    ASSERT(bytes);
    auto root = table_view<TupleFields>::from_bytes(*bytes);
    ASSERT(root.valid());
    EXPECT(root[&TupleFields::pair].get<0>() == 42);
    EXPECT(root[&TupleFields::pair].get<1>() == "hello");
    EXPECT(root[&TupleFields::triple].get<2>() == 3.14);
    EXPECT(root[&TupleFields::keyed].get<1>()[&test::Address::zip] == 10001);
    auto rows = root[&TupleFields::rows];
    ASSERT(rows.size() == 2U);
    EXPECT(rows[1].get<1>() == "c");
    // std::array is tuple-like: a tuple_view too.
    EXPECT(root[&TupleFields::nested].get<1>().get<2>() == 23);
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
    ASSERT(bytes);
    auto root = table_view<Lists>::from_bytes(*bytes);
    ASSERT(root.valid());
    EXPECT(root[&Lists::words][1] == "world");
    EXPECT(root[&Lists::places][1][&test::Address::city] == "b");
    EXPECT(root[&Lists::points][1].y == 4);
    // Boxed elements read through their table; an absent one as its default.
    auto maybe = root[&Lists::maybe];
    ASSERT(maybe.size() == 3U);
    EXPECT(maybe[0] == 5);
    EXPECT(maybe[1] == 0);
    EXPECT(maybe[2] == 9);
    auto blobs = root[&Lists::blobs];
    ASSERT(blobs.size() == 2U);
    EXPECT(blobs[0][1] == std::byte{0xBB});
    EXPECT(blobs[1].size() == 0U);
    EXPECT(root[&Lists::grid][0][1] == 2);
}

ZEST_CASE(array_view_out_of_range_reads_default) {
    auto bytes = fbs::to_bytes(make_profile());
    ASSERT(bytes);
    auto scores = table_view<Profile>::from_bytes(*bytes)[&Profile::scores];
    ASSERT(scores.size() == 3U);
    EXPECT(scores[3] == 0);
    EXPECT(scores[100] == 0);
}

ZEST_CASE(map_view_reads_entries_in_key_order) {
    auto bytes = fbs::to_bytes(make_map_fields());
    ASSERT(bytes);
    auto root = table_view<MapFields>::from_bytes(*bytes);
    ASSERT(root.valid());
    auto by_name = root[&MapFields::by_name];
    ASSERT(by_name.size() == 3U);
    EXPECT(by_name.at(0).get<0>() == "alpha");
    EXPECT(by_name.at(2).get<1>() == 3);
    auto places = root[&MapFields::places];
    ASSERT(places.size() == 2U);
    EXPECT(places.at(1).get<0>() == "work");
    EXPECT(places.at(1).get<1>()[&test::Address::zip] == 90001);
    EXPECT(root[&MapFields::none].empty());
    EXPECT(!by_name.at(3).valid());
}

ZEST_CASE(map_view_looks_up_string_keys) {
    auto bytes = fbs::to_bytes(make_map_fields());
    ASSERT(bytes);
    auto by_name = table_view<MapFields>::from_bytes(*bytes)[&MapFields::by_name];
    ASSERT(by_name.valid());
    EXPECT(by_name[std::string("beta")] == 2);
    EXPECT(by_name[std::string("missing")] == 0);
    auto found = by_name.find(std::string("gamma"));
    ASSERT(found);
    EXPECT(found->get<0>() == "gamma");
    EXPECT(found->get<1>() == 3);
    EXPECT(!by_name.find(std::string("missing")));
    EXPECT(by_name.contains(std::string("alpha")));
    EXPECT(!by_name.contains(std::string("missing")));
    // A table value reads as a nested view; a missing one as an invalid view.
    auto places = table_view<MapFields>::from_bytes(*bytes)[&MapFields::places];
    ASSERT(places.valid());
    EXPECT(places["work"][&test::Address::city] == "la");
    EXPECT(!places["nowhere"].valid());
}

ZEST_CASE(map_view_looks_up_transparently) {
    auto bytes = fbs::to_bytes(make_map_fields());
    ASSERT(bytes);
    auto by_name = table_view<MapFields>::from_bytes(*bytes)[&MapFields::by_name];
    ASSERT(by_name.valid());
    EXPECT(by_name["beta"] == 2);
    EXPECT(by_name.contains("alpha"));
    EXPECT(!by_name.contains("missing"));
    EXPECT(by_name[std::string_view("gamma")] == 3);
    auto found = by_name.find("beta");
    ASSERT(found);
    EXPECT(found->get<1>() == 2);
    EXPECT(!by_name.find("nope"));
}

ZEST_CASE(map_view_looks_up_integer_and_enum_keys) {
    // The binary search compares numbers as numbers (2 before 10), unsigned
    // keys as unsigned, and enums by their underlying value, as the encoder
    // sorted them.
    auto bytes = fbs::to_bytes(make_map_fields());
    ASSERT(bytes);
    auto root = table_view<MapFields>::from_bytes(*bytes);
    ASSERT(root.valid());
    auto by_id = root[&MapFields::by_id];
    EXPECT(by_id[-7] == "minus");
    EXPECT(by_id[2] == "two");
    EXPECT(by_id[10] == "ten");
    EXPECT(by_id[30] == "thirty");
    EXPECT(by_id[99] == "");
    auto by_sign = root[&MapFields::by_sign];
    EXPECT(by_sign[test::SignedEnum::neg] == 1);
    EXPECT(by_sign[test::SignedEnum::pos] == 3);
    auto by_wide_id = root[&MapFields::by_wide_id];
    EXPECT(by_wide_id[42U] == 3);
    EXPECT(by_wide_id[0x8000000000000001ULL] == 2);
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
    ASSERT(bytes);
    auto hits = table_view<StructKeyed>::from_bytes(*bytes)[&StructKeyed::hits];
    ASSERT(hits.size() == 3U);
    EXPECT(hits[keys[0]] == 1);
    EXPECT(hits[keys[1]] == 2);
    EXPECT(hits[keys[2]] == 3);
    auto found = hits.find(keys[1]);
    ASSERT(found);
    EXPECT(found->get<0>() == keys[1]);
    // Misses before the first entry, between two, and after the last.
    const OccurrenceKey between{
        .range = {.begin = 1, .end = 0},
        .target = 9,
        .weight = 0
    };
    EXPECT(!hits.contains(OccurrenceKey{
        .range = {.begin = 0, .end = 0},
        .target = 0,
        .weight = 0
    }));
    EXPECT(!hits.contains(between));
    EXPECT(!hits.contains(OccurrenceKey{
        .range = {.begin = 9, .end = 0},
        .target = 0,
        .weight = 0
    }));
    EXPECT(!hits.find(between));

    auto empty = fbs::to_bytes(StructKeyed{});
    ASSERT(empty);
    EXPECT(!table_view<StructKeyed>::from_bytes(*empty)[&StructKeyed::hits].contains(keys[0]));
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
    ASSERT(bytes);
    auto root = table_view<Reprs>::from_bytes(*bytes);
    ASSERT(root.valid());
    EXPECT(root[&Reprs::relation] == 102U);
    EXPECT(root[&Reprs::hex] == "0000beef");
    EXPECT(root[&Reprs::journal] == 41);
    EXPECT(root[&Reprs::journals][1] == 9);
    // A null repr is boxed; absent, it reads as the inner default.
    EXPECT(root[&Reprs::stamps][0] == 7U);
    EXPECT(root[&Reprs::stamps][1] == 0U);
    EXPECT(root[&Reprs::by_relation][102U] == 20);
    EXPECT(root[&Reprs::relation_by_name]["b"] == 102U);
    EXPECT(root[&Reprs::packed] == 3014U);
    EXPECT(root[&Reprs::maybe_decimal] == "12");
    EXPECT(root[&Reprs::widened] == 1234);
    EXPECT(root[&Reprs::grant][&test::AccessGrant::level] == "fullControl");
}

ZEST_CASE(from_verified_bytes_wraps_without_verifying) {
    // Verify once when a blob is loaded, then build views per query.
    auto bytes = fbs::to_bytes(make_profile());
    ASSERT(bytes);
    ASSERT(table_view<Profile>::from_bytes(*bytes).valid());
    auto root = table_view<Profile>::from_verified_bytes(*bytes);
    ASSERT(root.valid());
    EXPECT(root[&Profile::name] == "alice");
    auto as_bytes = std::as_bytes(std::span(*bytes));
    EXPECT(table_view<Profile>::from_verified_bytes(as_bytes)[&Profile::id] == 7);
}

};  // ZEST_SUITE(codec_fbs_proxy)

}  // namespace

}  // namespace kota::codec
