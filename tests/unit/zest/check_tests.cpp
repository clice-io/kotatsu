#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

ZEST_SUITE(zest_check){

    ZEST_CASE(comparison_splits_into_operands){auto split = (Decomposer{} << 1) == 2;
EXPECT(!split.held);
EXPECT(split.lhs == 1);
EXPECT(split.rhs == 2);
EXPECT(holds((Decomposer{} << 3) < 4));
EXPECT(!holds((Decomposer{} << 4) <= 3));

}  // namespace

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
    EXPECT(starts_with(std::string("prefix-body"), "prefix"));
    EXPECT(ends_with(std::string("body-suffix"), "suffix"));
    EXPECT(type_eq<int, int>());
    EXPECT(!type_eq<int, long>());
}

ZEST_CASE(negation_keeps_the_explanation) {
    auto match = contains(std::string("abc"), "x");
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
    {
        ZEST_CONTEXT("outer {}", 1);
        ZEST_CONTEXT("inner");
        EXPECT(1 == 1);
    }
    EXPECT(true);
}

};  // namespace kota::zest

}  // namespace

}  // namespace kota::zest
