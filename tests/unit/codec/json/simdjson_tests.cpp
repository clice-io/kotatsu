#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "fixtures/schema/common.h"
#include "kota/zest/zest.h"
#include "kota/codec/json/json.h"

namespace kota::codec {

using namespace meta;

namespace {

using json::from_string;
using json::from_string;
using json::to_string;

using person = meta::fixtures::PersonWithScores;

struct object_int_value {
    int value = 0;
};

struct object_string_value {
    std::string value;
};

enum class signed_enum : std::int8_t {
    low = -3,
    high = 7,
};

enum class unsigned_enum : std::uint8_t {
    zero = 0,
    max = 250,
};

enum class char_enum : char {
    a = 'A',
    z = 'Z',
};

ZEST_SUITE(serde_simdjson) {

ZEST_CASE(basic_roundtrip) {
    ASSERT(to_string(true) == "true");
    ASSERT(to_string(static_cast<std::int64_t>(-7)) == "-7");
    ASSERT(to_string(static_cast<std::uint64_t>(42)) == "42");
    ASSERT(to_string(3.5) == "3.5");
    ASSERT(to_string('x') == R"("x")");
    ASSERT(to_string(std::string("ok")) == R"("ok")");
    ASSERT(to_string(nullptr) == "null");

    bool b = false;
    ASSERT(from_string("true", b).has_value());
    EXPECT(b == true);

    std::int64_t i = 0;
    ASSERT(from_string("-7", i).has_value());
    EXPECT(i == -7);

    std::uint64_t u = 0;
    ASSERT(from_string("42", u).has_value());
    EXPECT(u == 42U);

    double f = 0.0;
    ASSERT(from_string("3.5", f).has_value());
    EXPECT(f == 3.5);

    char c = '\0';
    ASSERT(from_string(R"("x")", c).has_value());
    EXPECT(c == 'x');

    std::string s;
    ASSERT(from_string(R"("ok")", s).has_value());
    EXPECT(s == "ok");

    std::nullptr_t n = nullptr;
    ASSERT(from_string("null", n).has_value());
    EXPECT(n == nullptr);
}

ZEST_CASE(basic_errors) {
    bool b = false;
    auto bool_status = from_string("1", b);
    EXPECT(!bool_status);

    int i = 0;
    auto int_status = from_string(R"("7")", i);
    EXPECT(!int_status);

    std::uint8_t u8 = 0;
    auto u8_status = from_string("300", u8);
    EXPECT(!u8_status);

    char c = '\0';
    auto char_status = from_string(R"("xy")", c);
    EXPECT(!char_status);

    std::string s;
    auto str_status = from_string("null", s);
    EXPECT(!str_status);

    std::nullptr_t n = nullptr;
    auto null_status = from_string("0", n);
    EXPECT(!null_status);
}

ZEST_CASE(char_codepoint_range) {
    // U+20AC does not fit char: the decode errors instead of silently
    // truncating the codepoint.
    char c = '\0';
    EXPECT(!from_string(R"("€")", c).has_value());

    // A multi-byte codepoint whose value fits the octet range still decodes.
    ASSERT(from_string(R"("é")", c).has_value());
    EXPECT(c == static_cast<char>(0xE9));

    // Exact boundary: U+00FF is the last codepoint that fits, U+0100 the
    // first that does not.
    ASSERT(from_string(R"("ÿ")", c).has_value());
    EXPECT(c == static_cast<char>(0xFF));
    EXPECT(!from_string(R"("Ā")", c).has_value());

    // Encode maps the octet back to the same codepoint (no sign extension),
    // so the pair roundtrips both ways.
    EXPECT(to_string(static_cast<char>(0xE9)) == R"("é")");
}

ZEST_CASE(enum_roundtrip) {
    ASSERT(to_string(signed_enum::low) == "-3");
    ASSERT(to_string(unsigned_enum::max) == "250");
    // char-backed enum still serializes as underlying integer, not as a JSON string.
    ASSERT(to_string(char_enum::a) == "65");

    signed_enum signed_out = signed_enum::high;
    ASSERT(from_string("-3", signed_out).has_value());
    EXPECT(signed_out == signed_enum::low);

    unsigned_enum unsigned_out = unsigned_enum::zero;
    ASSERT(from_string("250", unsigned_out).has_value());
    EXPECT(unsigned_out == unsigned_enum::max);

    char_enum char_out = char_enum::a;
    ASSERT(from_string("90", char_out).has_value());
    EXPECT(char_out == char_enum::z);
}

ZEST_CASE(enum_errors) {
    unsigned_enum unsigned_out = unsigned_enum::zero;
    auto negative_error = from_string("-1", unsigned_out);
    EXPECT(!negative_error);
    EXPECT(unsigned_out == unsigned_enum::zero);

    auto overflow_error = from_string("300", unsigned_out);
    EXPECT(!overflow_error);
    EXPECT(unsigned_out == unsigned_enum::zero);

    signed_enum signed_out = signed_enum::high;
    auto type_error = from_string(R"("x")", signed_out);
    EXPECT(!type_error);
    EXPECT(signed_out == signed_enum::high);
}

ZEST_CASE(array_roundtrip) {
    std::vector<int> ints{1, 2, 3, 5};
    ASSERT(to_string(ints) == R"([1,2,3,5])");

    std::vector<int> ints_out;
    ASSERT(from_string(R"([1,2,3,5])", ints_out).has_value());
    EXPECT(ints_out == std::vector<int>({1, 2, 3, 5}));

    std::tuple<int, bool, std::string, double> mixed{7, true, "ok", 1.25};
    ASSERT(to_string(mixed) == R"([7,true,"ok",1.25])");

    std::tuple<int, bool, std::string, double> mixed_out{};
    ASSERT(from_string(R"([7,true,"ok",1.25])", mixed_out).has_value());
    EXPECT(std::get<0>(mixed_out) == 7);
    EXPECT(std::get<1>(mixed_out) == true);
    EXPECT(std::get<2>(mixed_out) == "ok");
    EXPECT(std::get<3>(mixed_out) == 1.25);

    std::array<int, 3> fixed{4, 5, 6};
    ASSERT(to_string(fixed) == R"([4,5,6])");

    std::array<int, 3> fixed_out{};
    ASSERT(from_string(R"([4,5,6])", fixed_out).has_value());
    EXPECT(fixed_out == fixed);
}

ZEST_CASE(array_errors) {
    std::vector<int> ints;
    auto vector_shape_error = from_string(R"({"not":"array"})", ints);
    EXPECT(!vector_shape_error);

    auto vector_element_error = from_string(R"([1,"x",3])", ints);
    EXPECT(!vector_element_error);

    std::tuple<int, std::string> pair{};
    auto tuple_length_error = from_string(R"([1])", pair);
    EXPECT(!tuple_length_error);

    auto tuple_type_error = from_string(R"([1,2])", pair);
    EXPECT(!tuple_type_error);

    // Too many elements for tuple
    std::tuple<int, int> t2{};
    auto tuple_too_long = from_string(R"([1,2,3])", t2);
    EXPECT(!tuple_too_long);

    // Too many elements for pair
    std::pair<int, int> p2{};
    auto pair_too_long = from_string(R"([1,2,3])", p2);
    EXPECT(!pair_too_long);

    // Too few for pair
    auto pair_too_short = from_string(R"([1])", p2);
    EXPECT(!pair_too_short);

    // Empty array into non-empty tuple
    std::tuple<int> t1{};
    auto tuple_empty_src = from_string(R"([])", t1);
    EXPECT(!tuple_empty_src);

    // Non-empty array into empty tuple
    std::tuple<> t0{};
    auto tuple_empty_dst = from_string(R"([1])", t0);
    EXPECT(!tuple_empty_dst);

    std::array<int, 2> fixed{};
    auto fixed_short = from_string(R"([1])", fixed);
    EXPECT(!fixed_short);

    auto fixed_long = from_string(R"([1,2,3])", fixed);
    EXPECT(!fixed_long);

    auto fixed_type = from_string(R"([1,"x"])", fixed);
    EXPECT(!fixed_type);

    // Empty array into non-empty fixed array
    std::array<int, 1> fixed1{};
    auto fixed_empty_src = from_string(R"([])", fixed1);
    EXPECT(!fixed_empty_src);
}

ZEST_CASE(object_roundtrip) {
    person p{
        .id = 7,
        .name = "alice",
        .scores = {10, 20},
        .active = true,
    };

    ASSERT(to_string(p) == R"({"id":7,"name":"alice","scores":[10,20],"active":true})");

    person parsed{};
    ASSERT(from_string(R"({"id":7,"name":"alice","scores":[10,20],"active":true})", parsed)
               .has_value());
    EXPECT(parsed.id == 7);
    EXPECT(parsed.name == "alice");
    EXPECT(parsed.scores == std::vector<int>({10, 20}));
    EXPECT(parsed.active == true);
}

ZEST_CASE(object_errors) {
    person parsed{};

    auto shape_error = from_string(R"([1,2,3])", parsed);
    EXPECT(!shape_error);

    auto field_type_error =
        from_string(R"({"id":"bad","name":"alice","scores":[10,20],"active":true})", parsed);
    EXPECT(!field_type_error);
}

ZEST_CASE(map_roundtrip) {
    std::map<std::string, int> by_name{
        {"a", 1},
        {"b", 2}
    };
    ASSERT(to_string(by_name) == R"({"a":1,"b":2})");

    std::map<std::string, int> by_name_out;
    ASSERT(from_string(R"({"a":1,"b":2})", by_name_out).has_value());
    EXPECT(by_name_out == by_name);

    std::map<int, std::string> by_id{
        {1, "x"},
        {2, "y"}
    };
    ASSERT(to_string(by_id) == R"({"1":"x","2":"y"})");

    std::map<int, std::string> by_id_out;
    ASSERT(from_string(R"({"1":"x","2":"y"})", by_id_out).has_value());
    EXPECT(by_id_out == by_id);
}

ZEST_CASE(map_uint64_key_roundtrip) {
    // A key beyond int64::max travels as its exact decimal string — it must
    // not be routed through the signed rendering path.
    std::map<std::uint64_t, int> by_id{
        {9223372036854775809ull, 1}
    };
    ASSERT(to_string(by_id) == R"({"9223372036854775809":1})");

    std::map<std::uint64_t, int> out;
    ASSERT(from_string(R"({"9223372036854775809":1})", out).has_value());
    EXPECT(out == by_id);
}

ZEST_CASE(map_errors) {
    std::map<std::string, int> by_name;
    auto shape_error = from_string(R"([1,2,3])", by_name);
    EXPECT(!shape_error);

    auto value_type_error = from_string(R"({"a":"x"})", by_name);
    EXPECT(!value_type_error);

    std::map<int, int> by_id;
    auto key_parse_error = from_string(R"({"abc":1})", by_id);
    ASSERT(!key_parse_error);
    EXPECT(zest::contains(key_parse_error.error().message, "cannot parse map key 'abc'"));

    std::map<std::uint8_t, int> by_octet;
    EXPECT(!from_string(R"({"300":1})", by_octet).has_value());

    std::map<std::int8_t, int> by_offset;
    EXPECT(!from_string(R"({"300":1})", by_offset).has_value());

    std::map<std::uint32_t, int> by_index;
    EXPECT(!from_string(R"({"-1":1})", by_index).has_value());
}

ZEST_CASE(optional_roundtrip) {
    std::optional<int> some = 42;
    ASSERT(to_string(some) == "42");

    std::optional<int> none = std::nullopt;
    ASSERT(to_string(none) == "null");

    std::optional<int> out = std::nullopt;
    ASSERT(from_string("42", out).has_value());
    ASSERT(out);
    EXPECT(*out == 42);

    ASSERT(from_string("null", out).has_value());
    EXPECT(!out);
}

ZEST_CASE(optional_errors) {
    std::optional<int> out = std::nullopt;
    auto status = from_string(R"("x")", out);
    EXPECT(!status);
}

ZEST_CASE(variant_roundtrip) {
    using complex_variant = std::variant<int, std::string, std::vector<int>, person>;

    complex_variant as_int = 7;
    ASSERT(to_string(as_int) == "7");

    complex_variant as_string = std::string("ok");
    ASSERT(to_string(as_string) == R"("ok")");

    complex_variant as_array = std::vector<int>{1, 2, 3};
    ASSERT(to_string(as_array) == R"([1,2,3])");

    complex_variant as_object = person{.id = 1, .name = "alice", .scores = {9}, .active = true};
    ASSERT(to_string(as_object) == R"({"id":1,"name":"alice","scores":[9],"active":true})");

    complex_variant out = 0;
    ASSERT(from_string("7", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<int>(out) == 7);

    ASSERT(from_string(R"("ok")", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<std::string>(out) == "ok");

    ASSERT(from_string(R"([1,2,3])", out).has_value());
    EXPECT(out.index() == 2U);
    EXPECT(std::get<std::vector<int>>(out) == std::vector<int>({1, 2, 3}));

    ASSERT(from_string(R"({"id":1,"name":"alice","scores":[9],"active":true})", out).has_value());
    EXPECT(out.index() == 3U);
    const auto& parsed = std::get<person>(out);
    EXPECT(parsed.id == 1);
    EXPECT(parsed.name == "alice");
    EXPECT(parsed.scores == std::vector<int>({9}));
    EXPECT(parsed.active == true);
}

ZEST_CASE(variant_deep_scoring_disambiguation) {
    // Two structs with the same field name but different field types.
    // Deep scoring correctly picks the alternative whose field type matches the source.
    using object_variant = std::variant<object_int_value, object_string_value>;

    object_variant out = object_int_value{.value = 0};
    // Deep scoring: "text" is string → object_string_value wins
    auto string_status = from_string(R"({"value":"text"})", out);
    ASSERT(string_status);
    EXPECT(out.index() == 1U);
    EXPECT(std::get<object_string_value>(out).value == "text");

    // Deep scoring: 42 is int → object_int_value wins
    auto int_status = from_string(R"({"value":42})", out);
    ASSERT(int_status);
    EXPECT(out.index() == 0U);
    EXPECT(std::get<object_int_value>(out).value == 42);

    using strict_variant = std::variant<int, bool>;
    strict_variant strict_out = 0;
    auto no_match_status = from_string(R"({"x":1})", strict_out);
    EXPECT(!no_match_status);
}

ZEST_CASE(bytes_roundtrip) {
    std::array<std::byte, 4> bytes{std::byte{0}, std::byte{1}, std::byte{127}, std::byte{255}};
    ASSERT(to_string(std::span<const std::byte>(bytes)) == R"([0,1,127,255])");

    std::vector<std::byte> out;
    ASSERT(from_string(R"([0,1,127,255])", out).has_value());
    ASSERT(out.size() == 4U);
    EXPECT(std::to_integer<int>(out[0]) == 0);
    EXPECT(std::to_integer<int>(out[1]) == 1);
    EXPECT(std::to_integer<int>(out[2]) == 127);
    EXPECT(std::to_integer<int>(out[3]) == 255);

    auto range_error = from_string(R"([0,256])", out);
    EXPECT(!range_error);
}

ZEST_CASE(misc_behavior) {
    auto out = to_string(std::numeric_limits<double>::infinity());
    ASSERT(out == "null");

    std::vector<int> value{7, 9};
    ASSERT(to_string(value, 1) == R"([7,9])");

    auto first = to_string(true);
    ASSERT(first == "true");
    auto second = to_string(value);
    ASSERT(second == R"([7,9])");

    auto from_value = from_string<std::vector<int>>(R"([7,9])");
    ASSERT(from_value == std::vector<int>({7, 9}));
}

};  // ZEST_SUITE(serde_simdjson)

struct StrictStruct {
    int x{};
    std::string name;
};

struct MixedStruct {
    int required_field{};
    std::optional<int> optional_field;
    KOTATSU_ANNOTATE(defaulted = true)
    <std::string> defaulted_field;
};

struct AllOptional {
    std::optional<int> a;
    std::optional<std::string> b;
};

ZEST_SUITE(serde_required_fields) {

ZEST_CASE(missing_required_field_fails) {
    // "name" is required (non-optional), missing → error
    auto result = from_string<StrictStruct>(R"({"x": 42})");
    EXPECT(!result);
}

ZEST_CASE(all_required_fields_present_succeeds) {
    auto result = from_string<StrictStruct>(R"({"x": 42, "name": "hello"})");
    ASSERT(result);
    EXPECT(result->x == 42);
    EXPECT(result->name == "hello");
}

ZEST_CASE(empty_json_object_fails_if_required_fields) {
    auto result = from_string<StrictStruct>(R"({})");
    EXPECT(!result);
}

ZEST_CASE(optional_field_can_be_absent) {
    auto result = from_string<MixedStruct>(R"({"required_field": 7})");
    ASSERT(result);
    EXPECT(result->required_field == 7);
    EXPECT(!result->optional_field);
    EXPECT(result->defaulted_field == std::string{});
}

ZEST_CASE(defaulted_field_can_be_absent) {
    auto result = from_string<MixedStruct>(R"({"required_field": 1})");
    ASSERT(result);
    EXPECT(result->defaulted_field == std::string{});
}

ZEST_CASE(defaulted_field_present_is_used) {
    auto result = from_string<MixedStruct>(R"({"required_field": 1, "defaulted_field": "hi"})");
    ASSERT(result);
    EXPECT(result->defaulted_field == std::string{"hi"});
}

ZEST_CASE(all_optional_struct_empty_object_succeeds) {
    auto result = from_string<AllOptional>(R"({})");
    ASSERT(result);
    EXPECT(!result->a);
    EXPECT(!result->b);
}

ZEST_CASE(unknown_fields_ignored_by_default) {
    auto result = from_string<StrictStruct>(R"({"x": 1, "name": "ok", "extra": true})");
    ASSERT(result);
    EXPECT(result->x == 1);
}

};  // ZEST_SUITE(serde_required_fields)

}  // namespace

}  // namespace kota::codec
