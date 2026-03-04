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

#include "eventide/zest/zest.h"
#include "eventide/serde/json/simd_deserializer.h"
#include "eventide/serde/json/simd_serializer.h"
#include "eventide/serde/serde.h"

namespace eventide::serde {

namespace {

using json::simd::from_json;
using json::simd::to_json;

struct person {
    int id = 0;
    std::string name;
    std::vector<int> scores;
    bool active = false;
};

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

TEST_SUITE(serde_simdjson) {

TEST_CASE(basic_roundtrip) {
    ASSERT_EQ(to_json(true), "true");
    ASSERT_EQ(to_json(static_cast<std::int64_t>(-7)), "-7");
    ASSERT_EQ(to_json(static_cast<std::uint64_t>(42)), "42");
    ASSERT_EQ(to_json(3.5), "3.5");
    ASSERT_EQ(to_json('x'), R"("x")");
    ASSERT_EQ(to_json(std::string("ok")), R"("ok")");
    ASSERT_EQ(to_json(nullptr), "null");

    bool b = false;
    ASSERT_TRUE(from_json("true", b).has_value());
    EXPECT_EQ(b, true);

    std::int64_t i = 0;
    ASSERT_TRUE(from_json("-7", i).has_value());
    EXPECT_EQ(i, -7);

    std::uint64_t u = 0;
    ASSERT_TRUE(from_json("42", u).has_value());
    EXPECT_EQ(u, 42U);

    double f = 0.0;
    ASSERT_TRUE(from_json("3.5", f).has_value());
    EXPECT_EQ(f, 3.5);

    char c = '\0';
    ASSERT_TRUE(from_json(R"("x")", c).has_value());
    EXPECT_EQ(c, 'x');

    std::string s;
    ASSERT_TRUE(from_json(R"("ok")", s).has_value());
    EXPECT_EQ(s, "ok");

    std::nullptr_t n = nullptr;
    ASSERT_TRUE(from_json("null", n).has_value());
    EXPECT_EQ(n, nullptr);
}

TEST_CASE(basic_errors) {
    bool b = false;
    auto bool_status = from_json("1", b);
    EXPECT_FALSE(bool_status.has_value());

    int i = 0;
    auto int_status = from_json(R"("7")", i);
    EXPECT_FALSE(int_status.has_value());

    std::uint8_t u8 = 0;
    auto u8_status = from_json("300", u8);
    EXPECT_FALSE(u8_status.has_value());

    char c = '\0';
    auto char_status = from_json(R"("xy")", c);
    EXPECT_FALSE(char_status.has_value());

    std::string s;
    auto str_status = from_json("null", s);
    EXPECT_FALSE(str_status.has_value());

    std::nullptr_t n = nullptr;
    auto null_status = from_json("0", n);
    EXPECT_FALSE(null_status.has_value());
}

TEST_CASE(enum_roundtrip) {
    ASSERT_EQ(to_json(signed_enum::low), "-3");
    ASSERT_EQ(to_json(unsigned_enum::max), "250");
    // char-backed enum still serializes as underlying integer, not as a JSON string.
    ASSERT_EQ(to_json(char_enum::a), "65");

    signed_enum signed_out = signed_enum::high;
    ASSERT_TRUE(from_json("-3", signed_out).has_value());
    EXPECT_EQ(signed_out, signed_enum::low);

    unsigned_enum unsigned_out = unsigned_enum::zero;
    ASSERT_TRUE(from_json("250", unsigned_out).has_value());
    EXPECT_EQ(unsigned_out, unsigned_enum::max);

    char_enum char_out = char_enum::a;
    ASSERT_TRUE(from_json("90", char_out).has_value());
    EXPECT_EQ(char_out, char_enum::z);
}

TEST_CASE(enum_errors) {
    unsigned_enum unsigned_out = unsigned_enum::zero;
    auto negative_error = from_json("-1", unsigned_out);
    EXPECT_FALSE(negative_error.has_value());
    EXPECT_EQ(unsigned_out, unsigned_enum::zero);

    auto overflow_error = from_json("300", unsigned_out);
    EXPECT_FALSE(overflow_error.has_value());
    EXPECT_EQ(unsigned_out, unsigned_enum::zero);

    signed_enum signed_out = signed_enum::high;
    auto type_error = from_json(R"("x")", signed_out);
    EXPECT_FALSE(type_error.has_value());
    EXPECT_EQ(signed_out, signed_enum::high);
}

TEST_CASE(array_roundtrip) {
    std::vector<int> ints{1, 2, 3, 5};
    ASSERT_EQ(to_json(ints), R"([1,2,3,5])");

    std::vector<int> ints_out;
    ASSERT_TRUE(from_json(R"([1,2,3,5])", ints_out).has_value());
    EXPECT_EQ(ints_out, std::vector<int>({1, 2, 3, 5}));

    std::tuple<int, bool, std::string, double> mixed{7, true, "ok", 1.25};
    ASSERT_EQ(to_json(mixed), R"([7,true,"ok",1.25])");

    std::tuple<int, bool, std::string, double> mixed_out{};
    ASSERT_TRUE(from_json(R"([7,true,"ok",1.25])", mixed_out).has_value());
    EXPECT_EQ(std::get<0>(mixed_out), 7);
    EXPECT_EQ(std::get<1>(mixed_out), true);
    EXPECT_EQ(std::get<2>(mixed_out), "ok");
    EXPECT_EQ(std::get<3>(mixed_out), 1.25);
}

TEST_CASE(array_errors) {
    std::vector<int> ints;
    auto vector_shape_error = from_json(R"({"not":"array"})", ints);
    EXPECT_FALSE(vector_shape_error.has_value());

    auto vector_element_error = from_json(R"([1,"x",3])", ints);
    EXPECT_FALSE(vector_element_error.has_value());

    std::tuple<int, std::string> pair{};
    auto tuple_length_error = from_json(R"([1])", pair);
    EXPECT_FALSE(tuple_length_error.has_value());

    auto tuple_type_error = from_json(R"([1,2])", pair);
    EXPECT_FALSE(tuple_type_error.has_value());
}

TEST_CASE(object_roundtrip) {
    person p{
        .id = 7,
        .name = "alice",
        .scores = {10, 20},
        .active = true,
    };

    ASSERT_EQ(to_json(p), R"({"id":7,"name":"alice","scores":[10,20],"active":true})");

    person parsed{};
    ASSERT_TRUE(
        from_json(R"({"id":7,"name":"alice","scores":[10,20],"active":true})", parsed).has_value());
    EXPECT_EQ(parsed.id, 7);
    EXPECT_EQ(parsed.name, "alice");
    EXPECT_EQ(parsed.scores, std::vector<int>({10, 20}));
    EXPECT_EQ(parsed.active, true);
}

TEST_CASE(object_errors) {
    person parsed{};

    auto shape_error = from_json(R"([1,2,3])", parsed);
    EXPECT_FALSE(shape_error.has_value());

    auto field_type_error =
        from_json(R"({"id":"bad","name":"alice","scores":[10,20],"active":true})", parsed);
    EXPECT_FALSE(field_type_error.has_value());
}

TEST_CASE(map_roundtrip) {
    std::map<std::string, int> by_name{
        {"a", 1},
        {"b", 2}
    };
    ASSERT_EQ(to_json(by_name), R"({"a":1,"b":2})");

    std::map<std::string, int> by_name_out;
    ASSERT_TRUE(from_json(R"({"a":1,"b":2})", by_name_out).has_value());
    EXPECT_EQ(by_name_out, by_name);

    std::map<int, std::string> by_id{
        {1, "x"},
        {2, "y"}
    };
    ASSERT_EQ(to_json(by_id), R"({"1":"x","2":"y"})");

    std::map<int, std::string> by_id_out;
    ASSERT_TRUE(from_json(R"({"1":"x","2":"y"})", by_id_out).has_value());
    EXPECT_EQ(by_id_out, by_id);
}

TEST_CASE(map_errors) {
    std::map<std::string, int> by_name;
    auto shape_error = from_json(R"([1,2,3])", by_name);
    EXPECT_FALSE(shape_error.has_value());

    auto value_type_error = from_json(R"({"a":"x"})", by_name);
    EXPECT_FALSE(value_type_error.has_value());

    std::map<int, int> by_id;
    auto key_parse_error = from_json(R"({"abc":1})", by_id);
    EXPECT_FALSE(key_parse_error.has_value());
}

TEST_CASE(optional_roundtrip) {
    std::optional<int> some = 42;
    ASSERT_EQ(to_json(some), "42");

    std::optional<int> none = std::nullopt;
    ASSERT_EQ(to_json(none), "null");

    std::optional<int> out = std::nullopt;
    ASSERT_TRUE(from_json("42", out).has_value());
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out, 42);

    ASSERT_TRUE(from_json("null", out).has_value());
    EXPECT_FALSE(out.has_value());
}

TEST_CASE(optional_errors) {
    std::optional<int> out = std::nullopt;
    auto status = from_json(R"("x")", out);
    EXPECT_FALSE(status.has_value());
}

TEST_CASE(variant_roundtrip) {
    using complex_variant = std::variant<int, std::string, std::vector<int>, person>;

    complex_variant as_int = 7;
    ASSERT_EQ(to_json(as_int), "7");

    complex_variant as_string = std::string("ok");
    ASSERT_EQ(to_json(as_string), R"("ok")");

    complex_variant as_array = std::vector<int>{1, 2, 3};
    ASSERT_EQ(to_json(as_array), R"([1,2,3])");

    complex_variant as_object = person{.id = 1, .name = "alice", .scores = {9}, .active = true};
    ASSERT_EQ(to_json(as_object), R"({"id":1,"name":"alice","scores":[9],"active":true})");

    complex_variant out = 0;
    ASSERT_TRUE(from_json("7", out).has_value());
    EXPECT_EQ(out.index(), 0U);
    EXPECT_EQ(std::get<int>(out), 7);

    ASSERT_TRUE(from_json(R"("ok")", out).has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<std::string>(out), "ok");

    ASSERT_TRUE(from_json(R"([1,2,3])", out).has_value());
    EXPECT_EQ(out.index(), 2U);
    EXPECT_EQ(std::get<std::vector<int>>(out), std::vector<int>({1, 2, 3}));

    ASSERT_TRUE(
        from_json(R"({"id":1,"name":"alice","scores":[9],"active":true})", out).has_value());
    EXPECT_EQ(out.index(), 3U);
    const auto& parsed = std::get<person>(out);
    EXPECT_EQ(parsed.id, 1);
    EXPECT_EQ(parsed.name, "alice");
    EXPECT_EQ(parsed.scores, std::vector<int>({9}));
    EXPECT_EQ(parsed.active, true);
}

TEST_CASE(variant_backtrack_errors) {
    using object_variant = std::variant<object_int_value, object_string_value>;

    object_variant out = object_int_value{.value = 0};
    auto backtrack_status = from_json(R"({"value":"text"})", out);
    ASSERT_TRUE(backtrack_status.has_value());
    EXPECT_EQ(out.index(), 1U);
    EXPECT_EQ(std::get<object_string_value>(out).value, "text");

    using strict_variant = std::variant<int, bool>;
    strict_variant strict_out = 0;
    auto no_match_status = from_json(R"({"x":1})", strict_out);
    EXPECT_FALSE(no_match_status.has_value());
}

TEST_CASE(bytes_roundtrip) {
    std::array<std::byte, 4> bytes{std::byte{0}, std::byte{1}, std::byte{127}, std::byte{255}};
    ASSERT_EQ(to_json(std::span<const std::byte>(bytes)), R"([0,1,127,255])");

    std::vector<std::byte> out;
    ASSERT_TRUE(from_json(R"([0,1,127,255])", out).has_value());
    ASSERT_EQ(out.size(), 4U);
    EXPECT_EQ(std::to_integer<int>(out[0]), 0);
    EXPECT_EQ(std::to_integer<int>(out[1]), 1);
    EXPECT_EQ(std::to_integer<int>(out[2]), 127);
    EXPECT_EQ(std::to_integer<int>(out[3]), 255);

    auto range_error = from_json(R"([0,256])", out);
    EXPECT_FALSE(range_error.has_value());
}

TEST_CASE(misc_behavior) {
    auto out = to_json(std::numeric_limits<double>::infinity());
    ASSERT_EQ(out, "null");

    std::vector<int> value{7, 9};
    ASSERT_EQ(to_json(value, 1), R"([7,9])");

    auto first = to_json(true);
    ASSERT_EQ(first, "true");
    auto second = to_json(value);
    ASSERT_EQ(second, R"([7,9])");

    auto from_value = from_json<std::vector<int>>(R"([7,9])");
    ASSERT_EQ(from_value, std::vector<int>({7, 9}));
}

};  // TEST_SUITE(serde_simdjson)

}  // namespace

}  // namespace eventide::serde
