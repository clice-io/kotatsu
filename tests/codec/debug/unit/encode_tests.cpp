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

#include "codec/harness/fixtures/configs.h"
#include "codec/harness/fixtures/structs.h"
#include "fixtures/enums.h"
#include "kota/zest/zest.h"
#include "kota/codec/debug/encode.h"

namespace kota::codec::debug {

namespace {

/// Not an aggregate, so the schema knows nothing about it.
class Opaque {
    [[maybe_unused]] int hidden = 0;
};

struct HoldsOpaque {
    Opaque inner;
    int n;
};

test::Person ada() {
    return {
        .name = "Ada",
        .age = 36,
        .addr = {.city = "London", .zip = 1}
    };
}

ZEST_SUITE(codec_debug_encode) {

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
}

ZEST_CASE(integer_values) {
    EXPECT(to_string(42) == "42");
    EXPECT(to_string(-1) == "-1");
    EXPECT(to_string(std::uint64_t{100}) == "100");
}

ZEST_CASE(float_values) {
    EXPECT(to_string(3.14) == "3.14");
    EXPECT(to_string(0.0) == "0");
    EXPECT(to_string(1.0) == "1");
    EXPECT(to_string(-2.5) == "-2.5");
}

ZEST_CASE(float32_values) {
    EXPECT(to_string(1.0F) == "1");
    EXPECT(to_string(3.14F) == "3.140000104904175");
}

ZEST_CASE(float_special) {
    EXPECT(to_string(std::numeric_limits<double>::quiet_NaN()) == "nan");
    EXPECT(to_string(std::numeric_limits<double>::infinity()) == "inf");
    EXPECT(to_string(-std::numeric_limits<double>::infinity()) == "-inf");
}

ZEST_CASE(float_special_with_config) {
    EXPECT(to_string<test::NanStringConfig>(std::numeric_limits<double>::quiet_NaN()) ==
           R"("NaN")");
    EXPECT(to_string<test::NanStringConfig>(std::numeric_limits<double>::infinity()) ==
           R"("Infinity")");
}

ZEST_CASE(string_values) {
    EXPECT(to_string(std::string("hello")) == R"("hello")");
    EXPECT(to_string(std::string("")) == R"("")");
}

ZEST_CASE(string_escaping) {
    EXPECT(to_string(std::string("a\"b")) == R"("a\"b")");
    EXPECT(to_string(std::string("a\\b")) == R"("a\\b")");
    EXPECT(to_string(std::string("a\nb")) == R"("a\nb")");
    EXPECT(to_string(std::string("a\tb")) == R"("a\tb")");
    EXPECT(to_string(std::string("a\rb")) == R"("a\rb")");
    EXPECT(to_string(std::string(1, '\0')) == R"("\0")");
    EXPECT(to_string(std::string{'a', '\x01', 'b'}) == R"("a\x01b")");
}

ZEST_CASE(char_values) {
    EXPECT(to_string('a') == "'a'");
    EXPECT(to_string('\'') == R"('\'')");
    EXPECT(to_string('\\') == R"('\\')");
    EXPECT(to_string('\n') == R"('\n')");
    EXPECT(to_string('\0') == R"('\0')");
    EXPECT(to_string('\x01') == R"('\x01')");
}

ZEST_CASE(char_writes_its_codepoint) {
    // The char's value, 0-255, is the codepoint, in UTF-8 as json writes it.
    EXPECT(to_string(static_cast<char>(0xB5)) == "'µ'");
    // 0x80-0x9F are C1 controls, which print as their codepoints like the
    // rest; DEL, 0x7F, prints as the byte it is.
    EXPECT(to_string(static_cast<char>(0x80)) == "'\xC2\x80'");
    EXPECT(to_string(static_cast<char>(0x9F)) == "'\xC2\x9F'");
    EXPECT(to_string('\x7F') == "'\x7F'");
    EXPECT(to_string(static_cast<char>(0xE9)) == "'é'");
    EXPECT(to_string(static_cast<char>(0xFF)) == "'ÿ'");
}

ZEST_CASE(bytes_values) {
    std::byte data[] = {std::byte{0x00}, std::byte{0xab}, std::byte{0xff}};
    EXPECT(to_string(std::span<const std::byte>(data)) == "[0x00, 0xab, 0xff]");
    EXPECT(to_string(std::span<const std::byte>()) == "[]");
}

ZEST_CASE(optional_values) {
    EXPECT(to_string(std::optional<int>()) == "null");
    EXPECT(to_string(std::optional<int>(42)) == "42");
}

ZEST_CASE(enum_values) {
    EXPECT(to_string(test::Color::red) == "Color::red");
    EXPECT(to_string(test::Color::blue) == "Color::blue");
}

ZEST_CASE(enum_without_a_name) {
    EXPECT(to_string(static_cast<test::Color>(99)) == "Color::99");
}

ZEST_CASE(sequences_use_brackets) {
    EXPECT(to_string(std::vector<int>{1, 2, 3}) == "[1, 2, 3]");
    EXPECT(to_string(std::vector<int>{}) == "[]");
}

ZEST_CASE(sets_use_braces) {
    EXPECT(to_string(std::set<int>{1, 2, 3}) == "{1, 2, 3}");
    EXPECT(to_string(std::set<int>{}) == "{}");
}

ZEST_CASE(maps_show_keys) {
    EXPECT(to_string(std::map<std::string, int>{
               {"a", 1},
               {"b", 2}
    }) == R"({"a": 1, "b": 2})");
    EXPECT(to_string(std::map<int, std::string>{
               {1, "one"},
               {2, "two"}
    }) == R"({1: "one", 2: "two"})");
    EXPECT(to_string(std::map<std::string, int>{}) == "{}");
}

ZEST_CASE(tuples_use_parentheses) {
    EXPECT(to_string(std::make_tuple(1, std::string("two"), true)) == R"((1, "two", true))");
    EXPECT(to_string(std::make_tuple(42)) == "(42,)");
    EXPECT(to_string(std::make_tuple()) == "()");
}

ZEST_CASE(structs_show_names) {
    EXPECT(to_string(test::Point{.x = 10, .y = 20}) == "Point { x: 10, y: 20 }");
    EXPECT(to_string(ada()) ==
           R"(Person { name: "Ada", age: 36, addr: Address { city: "London", zip: 1 } })");
    EXPECT(to_string(test::OneKeyMaybeTwo{.a = 1, .b = 2}) == "OneKeyMaybeTwo { a: 1, b: 2 }");
    EXPECT(to_string(test::OneKeyMaybeTwo{.a = 1, .b = std::nullopt}) ==
           "OneKeyMaybeTwo { a: 1, b: null }");
    EXPECT(to_string(test::Empty{}) == "Empty { }");
}

ZEST_CASE(variants_show_the_alternative) {
    EXPECT(to_string(std::variant<int, std::string>(42)) == "42");
    EXPECT(to_string(std::variant<int, std::string>("hello")) == R"("hello")");
}

ZEST_CASE(nested_containers) {
    std::vector<std::vector<int>> rows = {
        {1, 2},
        {3, 4}
    };
    EXPECT(to_string(rows) == "[[1, 2], [3, 4]]");
}

ZEST_CASE(pointers_show_the_address) {
    int* null = nullptr;
    EXPECT(to_string(null) == "null");
    int x = 42;
    auto raw = to_string(&x);
    ASSERT(raw);
    EXPECT(zest::starts_with(*raw, "0x"));

    EXPECT(to_string(std::unique_ptr<int>()) == "null");
    auto unique = to_string(std::make_unique<int>(42));
    ASSERT(unique);
    EXPECT(zest::starts_with(*unique, "0x"));

    EXPECT(to_string(std::shared_ptr<int>()) == "null");
    auto shared = to_string(std::make_shared<int>(42));
    ASSERT(shared);
    EXPECT(zest::starts_with(*shared, "0x"));
}

ZEST_CASE(weak_pointers_show_the_address_while_alive) {
    auto owner = std::make_shared<int>(42);
    std::weak_ptr<int> weak = owner;
    auto alive = to_string(weak);
    ASSERT(alive);
    EXPECT(zest::starts_with(*alive, "0x"));

    owner.reset();
    EXPECT(to_string(weak) == "null");
}

ZEST_CASE(expected_shows_the_value_or_the_error) {
    EXPECT(to_string(std::expected<int, std::string>(42)) == "42");
    EXPECT(to_string(std::expected<int, std::string>(std::unexpected("oops"))) == R"("oops")");
    EXPECT(to_string(std::expected<void, std::string>()) == "null");
    EXPECT(to_string(std::expected<void, std::string>(std::unexpected("fail"))) == R"("fail")");
}

ZEST_CASE(pretty_struct) {
    EXPECT(to_string(test::Point{.x = 10, .y = 20}, true) == R"(Point {
    x: 10,
    y: 20,
})");
}

ZEST_CASE(pretty_nested_struct) {
    EXPECT(to_string(ada(), true) == R"(Person {
    name: "Ada",
    age: 36,
    addr: Address {
        city: "London",
        zip: 1,
    },
})");
}

ZEST_CASE(pretty_null_field) {
    EXPECT(to_string(test::OneKeyMaybeTwo{.a = 1, .b = std::nullopt}, true) == R"(OneKeyMaybeTwo {
    a: 1,
    b: null,
})");
}

ZEST_CASE(pretty_sequence) {
    EXPECT(to_string(std::vector<int>{1, 2, 3}, true) == R"([
    1,
    2,
    3,
])");
}

ZEST_CASE(pretty_empty_containers) {
    EXPECT(to_string(std::vector<int>{}, true) == "[]");
    EXPECT(to_string(std::set<int>{}, true) == "{}");
    EXPECT(to_string(std::map<std::string, int>{}, true) == "{}");
    EXPECT(to_string(test::Empty{}, true) == "Empty {}");
}

ZEST_CASE(pretty_structs_in_a_sequence) {
    std::vector<test::Point> points = {
        {.x = 1, .y = 2},
        {.x = 3, .y = 4}
    };
    EXPECT(to_string(points, true) == R"([
    Point {
        x: 1,
        y: 2,
    },
    Point {
        x: 3,
        y: 4,
    },
])");
}

ZEST_CASE(pretty_map) {
    std::map<std::string, int> by_name = {
        {"a", 1},
        {"b", 2}
    };
    EXPECT(to_string(by_name, true) == R"({
    "a": 1,
    "b": 2,
})");
}

ZEST_CASE(pretty_set) {
    EXPECT(to_string(std::set<int>{1, 2, 3}, true) == R"({
    1,
    2,
    3,
})");
}

ZEST_CASE(pretty_tuple) {
    EXPECT(to_string(std::make_tuple(1, std::string("two"), true), true) == R"((
    1,
    "two",
    true,
))");
}

ZEST_CASE(pretty_expected) {
    std::expected<test::Point, std::string> point = test::Point{.x = 1, .y = 2};
    EXPECT(to_string(point, true) == R"(Point {
    x: 1,
    y: 2,
})");
}

ZEST_CASE(pretty_scalars_unchanged) {
    EXPECT(to_string(42, true) == "42");
    EXPECT(to_string(true, true) == "true");
    EXPECT(to_string(std::string("hi"), true) == R"("hi")");
}

};  // ZEST_SUITE(codec_debug_encode)

}  // namespace

}  // namespace kota::codec::debug
