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
    // `T out{}` would copy-list-initialize the explicit list from `{}`.
    dyn::Value tree{
        {"list",  dyn::Array{std::int64_t{1}, std::int64_t{2}}},
        {"count", std::int64_t{2}                             }
    };
    auto result = dyn::from_dyn<test::HoldsExplicit>(tree);
    ASSERT(result);
    const test::HoldsExplicit expected{
        .list = {1, 2},
        .count = 2
    };
    EXPECT(meta::eq(*result, expected));
    STATIC_EXPECT(decodes_by_value<test::HoldsExplicit>);
    // A type with no default constructor has no value to decode into.
    STATIC_EXPECT(!decodes_by_value<test::NoDefault>);
}

ZEST_CASE(tree_reads_itself) {
    dyn::Value tree{
        {"k", dyn::Array{std::int64_t{9}, "x"}}
    };
    EXPECT(dyn::from_dyn<dyn::Value>(tree) == tree);
    EXPECT(dyn::from_dyn<dyn::Object>(tree) == tree.as_object());
    EXPECT(dyn::from_dyn<dyn::Array>(tree.as_object().at("k")) == dyn::Array{std::int64_t{9}, "x"});
}

ZEST_CASE(tree_of_another_kind_fails) {
    auto array = dyn::from_dyn<dyn::Array>(dyn::Value(std::int64_t{1}));
    ASSERT(!array);
    EXPECT(array.error().message == "invalid type: expected array, got signed_int");
    auto object = dyn::from_dyn<dyn::Object>(dyn::Value("x"));
    ASSERT(!object);
    EXPECT(object.error().message == "invalid type: expected object, got string");
}

ZEST_CASE(tree_inside_a_value_reads_itself) {
    // The tree is a temporary, which outlives the decode.
    auto typed = dyn::from_dyn<test::Field<dyn::Value>>(dyn::Value{
        {"value", dyn::Object{{"name", "alice"}, {"n", std::int64_t{1}}}},
    });
    ASSERT(typed);
    EXPECT(typed->value == (dyn::Value{
                               {"name", "alice"        },
                               {"n",    std::int64_t{1}},
    }));
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
    ASSERT(point);
    EXPECT(point->x == 3);
    auto map = dyn::from_dyn<std::map<std::string, int>>(tree);
    ASSERT(map);
    EXPECT(*map == (std::map<std::string, int>{
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
    ASSERT(!status);
    EXPECT(status.error().message == "adjacently tagged variant: duplicate tag field");
}

ZEST_CASE(internal_duplicate_tag_fails) {
    dyn::Object object;
    object.insert("kind", "circle");
    object.insert("kind", "rect");
    object.insert("radius", 1.0);
    test::InternalShape out;
    auto status = dyn::from_dyn(dyn::Value(object), out);
    ASSERT(!status);
    EXPECT(status.error().message == "internally tagged variant: duplicate tag field");
}

ZEST_CASE(adjacent_duplicate_content_fails) {
    dyn::Object object;
    object.insert("t", "number");
    object.insert("c", std::int64_t{1});
    object.insert("c", std::int64_t{2});
    test::AdjacentShape out;
    auto status = dyn::from_dyn(dyn::Value(object), out);
    ASSERT(!status);
    EXPECT(status.error().message == "adjacently tagged variant: duplicate content field");
}

ZEST_CASE(type_mismatch_fails) {
    bool flag = false;
    auto status = dyn::from_dyn(dyn::Value(std::int64_t{1}), flag);
    ASSERT(!status);
    EXPECT(status.error().message == "invalid type: expected boolean, got signed_int");
}

ZEST_CASE(null_from_non_null_fails) {
    std::nullptr_t null = nullptr;
    auto status = dyn::from_dyn(dyn::Value(std::int64_t{42}), null);
    ASSERT(!status);
    EXPECT(status.error().message == "invalid type: expected null, got signed_int");

    // An untagged variant's last alternative decodes on the real reader when
    // nothing else claims the value; a null alternative there must not
    // swallow it.
    std::variant<int, std::monostate> choice = 1;
    auto fallback = dyn::from_dyn(dyn::Value(std::string("x")), choice);
    ASSERT(!fallback);
    EXPECT(fallback.error().message == "invalid type: expected null, got string");
}

ZEST_CASE(integers_read_across_signedness) {
    std::int32_t signed_out = 0;
    ASSERT(dyn::from_dyn(dyn::Value(std::uint64_t{7}), signed_out));
    EXPECT(signed_out == 7);
    std::uint8_t unsigned_out = 0;
    ASSERT(dyn::from_dyn(dyn::Value(std::int64_t{7}), unsigned_out));
    EXPECT(unsigned_out == 7U);
}

ZEST_CASE(integer_out_of_range_fails) {
    // An integer that does not fit is out of range, whichever signedness
    // the tree stores it with.
    std::int8_t narrow = 0;
    auto wide = dyn::from_dyn(dyn::Value(std::int64_t{300}), narrow);
    ASSERT(!wide);
    EXPECT(wide.error().message == "integer value out of range");
    std::int64_t signed_out = 0;
    auto too_big = dyn::from_dyn(dyn::Value(std::numeric_limits<std::uint64_t>::max()), signed_out);
    ASSERT(!too_big);
    EXPECT(too_big.error().message == "integer value out of range");
    std::uint32_t unsigned_out = 0;
    auto negative = dyn::from_dyn(dyn::Value(std::int64_t{-1}), unsigned_out);
    ASSERT(!negative);
    EXPECT(negative.error().message == "integer value out of range");
}

ZEST_CASE(byte_out_of_range_fails) {
    std::vector<std::byte> bytes;
    auto status =
        dyn::from_dyn(dyn::Value(dyn::Array{std::uint64_t{0}, std::uint64_t{256}}), bytes);
    ASSERT(!status);
    EXPECT(status.error().message == "byte array element out of range [0, 255]");
}

ZEST_CASE(char_reads_one_codepoint_up_to_255) {
    // Two bytes of UTF-8 under either lead byte: C2 for U+0080-U+00BF, C3
    // above.
    char out = '\0';
    ASSERT(dyn::from_dyn(dyn::Value("\xC2\x80"), out));
    EXPECT(out == static_cast<char>(0x80));
    ASSERT(dyn::from_dyn(dyn::Value("§"), out));
    EXPECT(out == static_cast<char>(0xA7));
    ASSERT(dyn::from_dyn(dyn::Value("é"), out));
    EXPECT(out == static_cast<char>(0xE9));
    ASSERT(dyn::from_dyn(dyn::Value("ÿ"), out));
    EXPECT(out == static_cast<char>(0xFF));
}

ZEST_CASE(char_from_other_text_fails) {
    // A lone octet above 0x7F is not UTF-8, nor is a lead byte followed by
    // one that does not continue it; "Ā" and "€" do not fit a char, and "xy"
    // is two characters.
    for(std::string_view text: {"\xE9", "\xC3\x28", "Ā", "€", "xy", ""}) {
        ZEST_CONTEXT("text: {}", text);
        char out = '\0';
        auto status = dyn::from_dyn(dyn::Value(text), out);
        ASSERT(!status);
        EXPECT(status.error().message == codec::invalid_char_message);
    }
}

};  // ZEST_SUITE(codec_dyn_decode)

}  // namespace

}  // namespace kota::codec
