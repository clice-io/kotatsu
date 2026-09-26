#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "kota/zest/zest.h"

namespace kota::zest {

namespace {

struct Point {
    int x;
    int y;
};

std::expected<int, std::string> parse(std::string_view text) {
    if(text == "42") {
        return 42;
    }
    return std::unexpected(std::string(text));
}

ZEST_SUITE(zest_check) {

ZEST_CASE(comparison_splits_into_operands) {
    int one = 1;
    int two = 2;
    auto split = (detail::Decomposer{} << one) == two;
    EXPECT(!split.held);
    EXPECT(&split.lhs == &one);
    EXPECT(&split.rhs == &two);
    EXPECT(((detail::Decomposer{} << 3) < 4).held);
    EXPECT(!((detail::Decomposer{} << 4) <= 3).held);
}

ZEST_CASE(expected_and_optional_compare_by_value) {
    std::expected<int, std::string> ok = 42;
    std::expected<int, std::string> err = std::unexpected(std::string("boom"));
    std::optional<int> some = 42;
    std::optional<int> none = std::nullopt;

    EXPECT(ok == 42);
    EXPECT(42 == ok);
    EXPECT(ok != err);
    EXPECT(err != 42);
    EXPECT(ok == some);
    EXPECT(some == ok);
    EXPECT(some != none);
    EXPECT(none != 42);
}

ZEST_CASE(expected_and_optional_equality_both_ways) {
    std::expected<int, std::string> ok_42 = 42;
    std::expected<int, std::string> ok_7 = 7;
    std::expected<int, std::string> err_boom = std::unexpected(std::string("boom"));
    std::expected<int, std::string> err_boom_too = std::unexpected(std::string("boom"));
    std::expected<int, std::string> err_oops = std::unexpected(std::string("oops"));
    std::optional<int> some_42 = 42;
    std::optional<int> some_7 = 7;
    std::optional<int> none = std::nullopt;
    std::optional<int> none_too = std::nullopt;

    EXPECT(err_boom == err_boom_too);
    EXPECT(err_boom != err_oops);
    EXPECT(ok_42 != 7);
    EXPECT(7 != ok_42);
    EXPECT(none == none_too);
    EXPECT(some_42 != some_7);
    EXPECT(7 != some_42);
    EXPECT(ok_7 != some_42);
    EXPECT(some_42 != ok_7);
    EXPECT(ok_42 != none);
    EXPECT(none != ok_42);
    EXPECT(err_boom != some_42);
    EXPECT(some_42 != err_boom);
    EXPECT(err_boom != none);
    EXPECT(none != err_boom);
}

ZEST_CASE(unary_checks_convert_to_bool) {
    auto ok = parse("42");
    ASSERT(ok);
    EXPECT(*ok == 42);
    EXPECT(!parse("x"));
    EXPECT(std::optional<int>(1));
}

ZEST_CASE(orderings) {
    EXPECT(1 < 2);
    EXPECT(1 <= 1);
    EXPECT(2 > 1);
    EXPECT(2 >= 2);
    EXPECT(std::string("alpha") < std::string("beta"));
    EXPECT(-1 < 1u);
}

ZEST_CASE(operands_compare_structurally) {
    EXPECT(Point{1, 2} == Point{1, 2});
    EXPECT(Point{1, 2} != Point{2, 1});
    EXPECT(std::vector<Point>{
               {1, 2}
    } == std::vector<Point>{{1, 2}});
    EXPECT(std::size_t{3} == 3);
}

ZEST_CASE(char_pointers_compare_as_text_against_text) {
    std::string text = "same";
    std::string copy = text;
    const char* pointer = text.c_str();
    const char* null = nullptr;

    EXPECT(pointer == "same");
    EXPECT("same" == pointer);
    EXPECT(pointer == std::string_view("same"));
    EXPECT(pointer != "other");
    EXPECT(null != "same");
    // Two pointers compare by address.
    EXPECT(pointer != copy.c_str());
    EXPECT(pointer == text.c_str());

    // An array's text ends with the array, null character or not.
    const char tag[4] = {'K', 'O', 'T', 'A'};
    const char* kota = "KOTA";
    EXPECT(kota == tag);
}

ZEST_CASE(temporaries_outlive_the_check) {
    EXPECT(std::string("a") + "b" == "ab");
    EXPECT(parse("42").value() == 42);
}

ZEST_CASE(predicates) {
    EXPECT(contains(std::string("haystack"), "st"));
    EXPECT(contains(std::string_view("haystack"), 'k'));
    EXPECT(!contains(std::string("haystack"), "needle"));
    EXPECT(contains(std::vector<int>{1, 2, 3}, 2));
    EXPECT(!contains(std::vector<int>{1, 2, 3}, 4));
    std::string owned = "b";
    EXPECT(contains(std::vector<const char*>{"a", owned.c_str()}, "b"));
    EXPECT(starts_with(std::string("prefix-body"), "prefix"));
    EXPECT(ends_with(std::string("body-suffix"), "suffix"));
    // A null pattern is no text; an array's text ends with the array.
    const char* null = nullptr;
    const char tail[2] = {'x', 't'};
    EXPECT(!contains(std::string("text"), null));
    EXPECT(!starts_with(std::string("text"), null));
    EXPECT(ends_with(std::string("text"), tail));
    EXPECT(type_eq<int, int>());
    EXPECT(!type_eq<int, long>());
}

ZEST_CASE(negation_keeps_the_explanation) {
    std::string text = "abc";
    auto match = contains(text, "x");
    auto negated = !match;
    EXPECT(negated.held);
    EXPECT(negated.explain() == match.explain());
}

ZEST_CASE(static_checks) {
    constexpr int answer = 42;
    STATIC_EXPECT(answer == 42);
    STATIC_EXPECT(sizeof(int) >= 2);
    STATIC_EXPECT(std::is_integral_v<int>);
}

ZEST_CASE(contexts_nest_and_unwind) {
    ZEST_CONTEXT("outer {}", 1);
    {
        ZEST_CONTEXT("inner");
        EXPECT(1 == 1);
    }
    EXPECT(2 == 2);
}

};  // ZEST_SUITE(zest_check)

}  // namespace

}  // namespace kota::zest
