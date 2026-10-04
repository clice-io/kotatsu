#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "codec/harness/fixtures/containers.h"
#include "codec/harness/fixtures/structs.h"
#include "codec/harness/fixtures/tagged.h"
#include "kota/zest/zest.h"
#include "kota/meta/compare.h"
#include "kota/codec/dyn/dyn.h"

namespace kota::codec {

namespace {

/// Whether the value-returning overload takes T.
template <typename T>
concept decodes_by_value = requires(const dyn::Value& tree) { dyn::from_dyn<T>(tree); };

ZEST_SUITE(codec_dyn_decode) {

ZEST_CASE(value_overload_value_initializes) {
    // `T value{}` would copy-list-initialize the explicit list from `{}`.
    auto result = dyn::from_dyn<test::HoldsExplicit>(dyn::Value{
        {"list",  dyn::Array{std::int64_t{1}, std::int64_t{2}}},
        {"count", std::int64_t{2}                             },
    });
    ZASSERT(result);
    const test::HoldsExplicit expected{
        .list = {1, 2},
        .count = 2
    };
    ZEXPECT(meta::eq(*result, expected));
    ZSTATIC_EXPECT(decodes_by_value<test::HoldsExplicit>);
    // A type with no default constructor has no value to decode into.
    ZSTATIC_EXPECT(!decodes_by_value<test::NoDefault>);
}

ZEST_CASE(tree_reads_itself) {
    dyn::Value tree{
        {"k", dyn::Array{std::int64_t{9}, "x"}}
    };
    ZEXPECT(dyn::from_dyn<dyn::Value>(tree) == tree);
    ZEXPECT(dyn::from_dyn<dyn::Object>(tree) == tree.as_object());
    ZEXPECT(dyn::from_dyn<dyn::Array>(tree.as_object().at("k")) ==
            dyn::Array{std::int64_t{9}, "x"});
}

ZEST_CASE(tree_of_another_kind_fails) {
    auto array = dyn::from_dyn<dyn::Array>(dyn::Value(std::int64_t{1}));
    ZASSERT(!array);
    ZEXPECT(array.error().message == "invalid type: expected array, got signed_int");
    auto object = dyn::from_dyn<dyn::Object>(dyn::Value("x"));
    ZASSERT(!object);
    ZEXPECT(object.error().message == "invalid type: expected object, got string");
}

ZEST_CASE(tree_inside_a_value_reads_itself) {
    // The tree is a temporary, which outlives the decode.
    auto typed = dyn::from_dyn<test::Field<dyn::Value>>(dyn::Value{
        {"value", dyn::Object{{"name", "alice"}, {"n", std::int64_t{1}}}},
    });
    ZASSERT(typed);
    ZEXPECT(typed->value == (dyn::Value{
                                {"name", "alice"        },
                                {"n",    std::int64_t{1}},
    }));
}

ZEST_CASE(unknown_fields_reported_without_location) {
    UnknownFields unknown;
    scoped_context<UnknownFields> scope(unknown);
    auto point = dyn::from_dyn<test::Point>(dyn::Value{
        {"x",     std::int64_t{1}},
        {"y",     std::int64_t{2}},
        {"extra", true           },
    });
    ZASSERT(point);
    ZASSERT(unknown.entries.size() == 1U);
    ZEXPECT(unknown.entries[0].message == "unknown field 'extra'");
    ZEXPECT(!unknown.entries[0].location);
}

ZEST_CASE(duplicate_keys_last_wins) {
    // A tree may hold a key twice. The reader visits every entry in order,
    // so the last one wins, the one Object::find returns.
    dyn::Object object;
    object.insert("x", std::int64_t{1});
    object.insert("y", std::int64_t{2});
    object.insert("x", std::int64_t{3});
    dyn::Value tree(object);

    auto point = dyn::from_dyn<test::Point>(tree);
    ZASSERT(point);
    ZEXPECT(point->x == 3);
    auto map = dyn::from_dyn<std::map<std::string, int>>(tree);
    ZASSERT(map);
    ZEXPECT(*map == (std::map<std::string, int>{
                        {"x", 3},
                        {"y", 2}
    }));
}

ZEST_CASE(adjacent_duplicate_tag_fails) {
    // A tree may hold a key twice, as json text may.
    dyn::Object object;
    object.insert("t", "number");
    object.insert("t", "point");
    object.insert("c", std::int64_t{42});
    test::AdjacentShape out;
    auto status = dyn::from_dyn(dyn::Value(object), out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "adjacently tagged variant: duplicate tag field");
}

ZEST_CASE(internal_duplicate_tag_fails) {
    dyn::Object object;
    object.insert("kind", "circle");
    object.insert("kind", "rect");
    object.insert("radius", 1.0);
    test::InternalShape out;
    auto status = dyn::from_dyn(dyn::Value(object), out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "internally tagged variant: duplicate tag field");
}

ZEST_CASE(adjacent_duplicate_content_fails) {
    dyn::Object object;
    object.insert("t", "number");
    object.insert("c", std::int64_t{1});
    object.insert("c", std::int64_t{2});
    test::AdjacentShape out;
    auto status = dyn::from_dyn(dyn::Value(object), out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "adjacently tagged variant: duplicate content field");
}

ZEST_CASE(type_mismatch_fails) {
    bool flag = false;
    auto status = dyn::from_dyn(dyn::Value(std::int64_t{1}), flag);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "invalid type: expected boolean, got signed_int");
}

ZEST_CASE(null_from_non_null_fails) {
    std::nullptr_t null = nullptr;
    auto status = dyn::from_dyn(dyn::Value(std::int64_t{42}), null);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "invalid type: expected null, got signed_int");

    // An untagged variant's last alternative decodes on the real reader when
    // nothing else claims the value; a null alternative there must not
    // swallow it.
    std::variant<int, std::monostate> choice = 1;
    auto fallback = dyn::from_dyn(dyn::Value(std::string("x")), choice);
    ZASSERT(!fallback);
    ZEXPECT(fallback.error().message == "invalid type: expected null, got string");
}

ZEST_CASE(integers_read_across_signedness) {
    std::int32_t signed_out = 0;
    ZASSERT(dyn::from_dyn(dyn::Value(std::uint64_t{7}), signed_out));
    ZEXPECT(signed_out == 7);
    std::uint8_t unsigned_out = 0;
    ZASSERT(dyn::from_dyn(dyn::Value(std::int64_t{7}), unsigned_out));
    ZEXPECT(unsigned_out == 7U);
}

ZEST_CASE(integer_out_of_range_fails) {
    // An integer that does not fit is out of range, whichever signedness
    // the tree stores it with.
    std::int8_t narrow = 0;
    auto wide = dyn::from_dyn(dyn::Value(std::int64_t{300}), narrow);
    ZASSERT(!wide);
    ZEXPECT(wide.error().message == "integer value out of range");
    std::int64_t signed_out = 0;
    auto too_big = dyn::from_dyn(dyn::Value(std::numeric_limits<std::uint64_t>::max()), signed_out);
    ZASSERT(!too_big);
    ZEXPECT(too_big.error().message == "integer value out of range");
    std::uint32_t unsigned_out = 0;
    auto negative = dyn::from_dyn(dyn::Value(std::int64_t{-1}), unsigned_out);
    ZASSERT(!negative);
    ZEXPECT(negative.error().message == "integer value out of range");
}

ZEST_CASE(byte_out_of_range_fails) {
    std::vector<std::byte> bytes;
    auto status =
        dyn::from_dyn(dyn::Value(dyn::Array{std::uint64_t{0}, std::uint64_t{256}}), bytes);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "byte array element out of range [0, 255]");
}

ZEST_CASE(char_reads_one_codepoint_up_to_255) {
    // Two bytes of UTF-8 under either lead byte: C2 for U+0080-U+00BF, C3
    // above.
    char out = '\0';
    ZASSERT(dyn::from_dyn(dyn::Value("\xC2\x80"), out));
    ZEXPECT(out == static_cast<char>(0x80));
    ZASSERT(dyn::from_dyn(dyn::Value("§"), out));
    ZEXPECT(out == static_cast<char>(0xA7));
    ZASSERT(dyn::from_dyn(dyn::Value("é"), out));
    ZEXPECT(out == static_cast<char>(0xE9));
    ZASSERT(dyn::from_dyn(dyn::Value("ÿ"), out));
    ZEXPECT(out == static_cast<char>(0xFF));
}

ZEST_CASE(char_from_other_text_fails) {
    // A lone octet above 0x7F is not UTF-8, nor is a lead byte followed by
    // one that does not continue it; "Ā" and "€" do not fit a char, and "xy"
    // is two characters.
    for(std::string_view text: {"\xE9", "\xC3\x28", "Ā", "€", "xy", ""}) {
        ZEST_CONTEXT("text: {}", text);
        char out = '\0';
        auto status = dyn::from_dyn(dyn::Value(text), out);
        ZASSERT(!status);
        ZEXPECT(status.error().message == codec::invalid_char_message);
    }
}

};  // ZEST_SUITE(codec_dyn_decode)

}  // namespace

}  // namespace kota::codec
