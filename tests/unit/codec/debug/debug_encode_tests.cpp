#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <system_error>
#include <tuple>
#include <variant>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/codec/debug/encode.h"

namespace kota::codec::debug {

namespace {

enum class Color { Red, Green, Blue };

struct Point {
    int x;
    int y;
};

struct Person {
    std::string name;
    int age;
    std::optional<std::string> email;
};

/// Not an aggregate, so the schema knows nothing about it.
class Opaque {
    [[maybe_unused]] int hidden = 0;
};

struct HoldsOpaque {
    Opaque inner;
    int n;
};

struct nan_string_config {
    [[maybe_unused]] constexpr static auto nan_repr = codec::nan_repr::String;
};

}  // namespace

}  // namespace kota::codec::debug

namespace kota::codec::debug {

namespace {

ZEST_SUITE(debug_encode) {

ZEST_CASE(opaque_values) {
    auto code = to_string(std::make_error_code(std::errc::invalid_argument));
    ASSERT(code);
    EXPECT(zest::starts_with(*code, "generic: "));

    EXPECT(to_string(std::chrono::milliseconds(5)) == "5ms");

    auto named = to_string(Opaque{});
    ASSERT(named);
    EXPECT(zest::starts_with(*named, "<"));
    EXPECT(zest::ends_with(*named, "Opaque>"));

    auto held = to_string(HoldsOpaque{.inner = {}, .n = 1});
    ASSERT(held);
    EXPECT(zest::contains(*held, "inner: <"));
    EXPECT(zest::contains(*held, "n: 1"));
}

ZEST_CASE(expected_error_of_an_opaque_type) {
    std::expected<int, std::error_code> failed =
        std::unexpected(std::make_error_code(std::errc::invalid_argument));
    auto text = to_string(failed);
    ASSERT(text);
    EXPECT(zest::starts_with(*text, "generic: "));
}

ZEST_CASE(null_char_pointer) {
    const char* null = nullptr;
    EXPECT(to_string(null) == "null");
}

ZEST_CASE(char_arrays_end_with_the_array) {
    const char unterminated[4] = {'K', 'O', 'T', 'A'};
    const char padded[8] = "kota";
    EXPECT(to_string(unterminated) == R"("KOTA")");
    EXPECT(to_string(padded) == R"("kota")");
}

ZEST_CASE(bool_values) {
    EXPECT(to_string(true) == "true");
    EXPECT(to_string(false) == "false");
};

ZEST_CASE(integer_values) {
    EXPECT(to_string(42) == "42");
    EXPECT(to_string(-1) == "-1");
    EXPECT(to_string(std::uint64_t{100}) == "100");
};

ZEST_CASE(float_values) {
    EXPECT(to_string(3.14) == "3.14");
    EXPECT(to_string(0.0) == "0");
    EXPECT(to_string(1.0) == "1");
    EXPECT(to_string(-2.5) == "-2.5");
};

ZEST_CASE(float32_values) {
    EXPECT(to_string(1.0f) == "1");
    EXPECT(to_string(3.14f) == "3.140000104904175");
};

ZEST_CASE(float_special) {
    EXPECT(to_string(std::numeric_limits<double>::quiet_NaN()) == "nan");
    EXPECT(to_string(std::numeric_limits<double>::infinity()) == "inf");
    EXPECT(to_string(-std::numeric_limits<double>::infinity()) == "-inf");
};

ZEST_CASE(float_special_with_config) {
    EXPECT(to_string<nan_string_config>(std::numeric_limits<double>::quiet_NaN()) == R"("NaN")");
    EXPECT(to_string<nan_string_config>(std::numeric_limits<double>::infinity()) ==
           R"("Infinity")");
};

ZEST_CASE(string_values) {
    EXPECT(to_string(std::string("hello")) == R"("hello")");
    EXPECT(to_string(std::string("")) == R"("")");
};

ZEST_CASE(string_escaping) {
    EXPECT(to_string(std::string("a\"b")) == R"("a\"b")");
    EXPECT(to_string(std::string("a\\b")) == R"("a\\b")");
    EXPECT(to_string(std::string("a\nb")) == R"("a\nb")");
    EXPECT(to_string(std::string("a\tb")) == R"("a\tb")");
    EXPECT(to_string(std::string("a\rb")) == R"("a\rb")");
    EXPECT(to_string(std::string(1, '\0')) == R"("\0")");
};

ZEST_CASE(char_values) {
    EXPECT(to_string('a') == "'a'");
    EXPECT(to_string('\'') == R"('\'')");
    EXPECT(to_string('\\') == R"('\\')");
    EXPECT(to_string('\n') == R"('\n')");
    EXPECT(to_string('\0') == R"('\0')");
};

ZEST_CASE(bytes_values) {
    std::byte data[] = {std::byte{0x00}, std::byte{0xab}, std::byte{0xff}};
    std::span<const std::byte> bytes(data, 3);
    EXPECT(to_string(bytes) == "[0x00, 0xab, 0xff]");
};

ZEST_CASE(bytes_empty) {
    std::span<const std::byte> bytes;
    EXPECT(to_string(bytes) == "[]");
};

ZEST_CASE(null_optional) {
    std::optional<int> opt;
    EXPECT(to_string(opt) == "null");
};

ZEST_CASE(some_optional) {
    std::optional<int> opt = 42;
    EXPECT(to_string(opt) == "42");
};

ZEST_CASE(enum_value) {
    EXPECT(to_string(Color::Red) == "Color::Red");
    EXPECT(to_string(Color::Blue) == "Color::Blue");
};

ZEST_CASE(enum_out_of_range) {
    auto v = static_cast<Color>(99);
    EXPECT(to_string(v) == "Color::99");
};

ZEST_CASE(vector) {
    std::vector<int> v = {1, 2, 3};
    EXPECT(to_string(v) == "[1, 2, 3]");
};

ZEST_CASE(empty_vector) {
    std::vector<int> v;
    EXPECT(to_string(v) == "[]");
};

ZEST_CASE(set_uses_braces) {
    std::set<int> s = {1, 2, 3};
    EXPECT(to_string(s) == "{1, 2, 3}");
};

ZEST_CASE(empty_set) {
    std::set<int> s;
    EXPECT(to_string(s) == "{}");
};

ZEST_CASE(map) {
    std::map<std::string, int> m = {
        {"a", 1},
        {"b", 2}
    };
    EXPECT(to_string(m) == R"({"a": 1, "b": 2})");
};

ZEST_CASE(empty_map) {
    std::map<std::string, int> m;
    EXPECT(to_string(m) == "{}");
};

ZEST_CASE(map_int_keys) {
    std::map<int, std::string> m = {
        {1, "one"},
        {2, "two"}
    };
    EXPECT(to_string(m) == R"({1: "one", 2: "two"})");
};

ZEST_CASE(tuple) {
    auto t = std::make_tuple(1, std::string("two"), true);
    EXPECT(to_string(t) == R"((1, "two", true))");
};

ZEST_CASE(single_element_tuple) {
    auto t = std::make_tuple(42);
    EXPECT(to_string(t) == "(42,)");
};

ZEST_CASE(empty_tuple) {
    auto t = std::make_tuple();
    EXPECT(to_string(t) == "()");
};

ZEST_CASE(simple_struct) {
    Point p{10, 20};
    EXPECT(to_string(p) == "Point { x: 10, y: 20 }");
};

ZEST_CASE(nested_struct) {
    Person p{"Alice", 30, "alice@example.com"};
    EXPECT(to_string(p) == R"(Person { name: "Alice", age: 30, email: "alice@example.com" })");
};

ZEST_CASE(struct_with_null_optional) {
    Person p{"Bob", 25, std::nullopt};
    EXPECT(to_string(p) == R"(Person { name: "Bob", age: 25, email: null })");
};

ZEST_CASE(variant) {
    std::variant<int, std::string> v1 = 42;
    EXPECT(to_string(v1) == "42");
    std::variant<int, std::string> v2 = std::string("hello");
    EXPECT(to_string(v2) == R"("hello")");
};

ZEST_CASE(nested_containers) {
    std::vector<std::vector<int>> v = {
        {1, 2},
        {3, 4}
    };
    EXPECT(to_string(v) == "[[1, 2], [3, 4]]");
};

ZEST_CASE(raw_pointer) {
    int* null_ptr = nullptr;
    EXPECT(to_string(null_ptr) == "null");
    int x = 42;
    auto result = to_string(&x);
    EXPECT(result);
    EXPECT(zest::starts_with(*result, "0x"));
};

ZEST_CASE(unique_ptr_null) {
    std::unique_ptr<int> p;
    EXPECT(to_string(p) == "null");
};

ZEST_CASE(unique_ptr_value) {
    auto p = std::make_unique<int>(42);
    auto result = to_string(p);
    EXPECT(result);
    EXPECT(zest::starts_with(*result, "0x"));
};

ZEST_CASE(shared_ptr_null) {
    std::shared_ptr<int> p;
    EXPECT(to_string(p) == "null");
};

ZEST_CASE(shared_ptr_value) {
    auto p = std::make_shared<int>(42);
    auto result = to_string(p);
    EXPECT(result);
    EXPECT(zest::starts_with(*result, "0x"));
};

ZEST_CASE(weak_ptr_valid) {
    auto sp = std::make_shared<int>(42);
    std::weak_ptr<int> wp = sp;
    auto result = to_string(wp);
    EXPECT(result);
    EXPECT(zest::starts_with(*result, "0x"));
};

ZEST_CASE(weak_ptr_expired) {
    std::weak_ptr<int> wp;
    {
        auto sp = std::make_shared<int>(42);
        wp = sp;
    }
    EXPECT(to_string(wp) == "null");
};

ZEST_CASE(expected_value) {
    std::expected<int, std::string> e = 42;
    EXPECT(to_string(e) == "42");
};

ZEST_CASE(expected_error) {
    std::expected<int, std::string> e = std::unexpected(std::string("oops"));
    EXPECT(to_string(e) == R"("oops")");
};

ZEST_CASE(expected_void_value) {
    std::expected<void, std::string> e;
    EXPECT(to_string(e) == "null");
};

ZEST_CASE(expected_void_error) {
    std::expected<void, std::string> e = std::unexpected(std::string("fail"));
    EXPECT(to_string(e) == R"("fail")");
};

};  // ZEST_SUITE(debug_encode)

ZEST_SUITE(debug_encode_pretty) {

ZEST_CASE(simple_struct) {
    Point p{10, 20};
    auto expected = R"(Point {
    x: 10,
    y: 20,
})";
    EXPECT(to_string(p, true) == expected);
};

ZEST_CASE(nested_struct) {
    Person p{"Alice", 30, std::nullopt};
    auto expected = R"(Person {
    name: "Alice",
    age: 30,
    email: null,
})";
    EXPECT(to_string(p, true) == expected);
};

ZEST_CASE(vector) {
    std::vector<int> v = {1, 2, 3};
    auto expected = R"([
    1,
    2,
    3,
])";
    EXPECT(to_string(v, true) == expected);
};

ZEST_CASE(empty_containers) {
    EXPECT(to_string(std::vector<int>{}, true) == "[]");
    EXPECT(to_string(std::set<int>{}, true) == "{}");
    EXPECT(to_string(std::map<std::string, int>{}, true) == "{}");
};

ZEST_CASE(nested_pretty) {
    std::vector<Point> v = {
        {1, 2},
        {3, 4}
    };
    auto expected = R"([
    Point {
        x: 1,
        y: 2,
    },
    Point {
        x: 3,
        y: 4,
    },
])";
    EXPECT(to_string(v, true) == expected);
};

ZEST_CASE(map_pretty) {
    std::map<std::string, int> m = {
        {"a", 1},
        {"b", 2}
    };
    auto expected = R"({
    "a": 1,
    "b": 2,
})";
    EXPECT(to_string(m, true) == expected);
};

ZEST_CASE(set_pretty) {
    std::set<int> s = {1, 2, 3};
    auto expected = R"({
    1,
    2,
    3,
})";
    EXPECT(to_string(s, true) == expected);
};

ZEST_CASE(tuple_pretty) {
    auto t = std::make_tuple(1, std::string("two"), true);
    auto expected = R"((
    1,
    "two",
    true,
))";
    EXPECT(to_string(t, true) == expected);
};

ZEST_CASE(expected_pretty) {
    std::expected<Point, std::string> e = Point{1, 2};
    auto expected = R"(Point {
    x: 1,
    y: 2,
})";
    EXPECT(to_string(e, true) == expected);
};

ZEST_CASE(primitives_unchanged) {
    EXPECT(to_string(42, true) == "42");
    EXPECT(to_string(true, true) == "true");
    EXPECT(to_string(std::string("hi"), true) == R"("hi")");
};

};  // ZEST_SUITE(debug_encode_pretty)

}  // namespace
}  // namespace kota::codec::debug
