#include <expected>
#include <optional>
#include <stdexcept>
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
    ZEXPECT(!split.held);
    ZEXPECT(&split.lhs == &one);
    ZEXPECT(&split.rhs == &two);
    ZEXPECT(((detail::Decomposer{} << 3) < 4).held);
    ZEXPECT(!((detail::Decomposer{} << 4) <= 3).held);
}

ZEST_CASE(expected_and_optional_compare_by_value) {
    std::expected<int, std::string> ok = 42;
    std::expected<int, std::string> err = std::unexpected(std::string("boom"));
    std::optional<int> some = 42;
    std::optional<int> none = std::nullopt;

    ZEXPECT(ok == 42);
    ZEXPECT(42 == ok);
    ZEXPECT(ok != err);
    ZEXPECT(err != 42);
    ZEXPECT(ok == some);
    ZEXPECT(some == ok);
    ZEXPECT(some != none);
    ZEXPECT(none != 42);
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

    ZEXPECT(err_boom == err_boom_too);
    ZEXPECT(err_boom != err_oops);
    ZEXPECT(ok_42 != 7);
    ZEXPECT(7 != ok_42);
    ZEXPECT(none == none_too);
    ZEXPECT(some_42 != some_7);
    ZEXPECT(7 != some_42);
    ZEXPECT(ok_7 != some_42);
    ZEXPECT(some_42 != ok_7);
    ZEXPECT(ok_42 != none);
    ZEXPECT(none != ok_42);
    ZEXPECT(err_boom != some_42);
    ZEXPECT(some_42 != err_boom);
    ZEXPECT(err_boom != none);
    ZEXPECT(none != err_boom);
}

ZEST_CASE(unary_checks_convert_to_bool) {
    auto ok = parse("42");
    ZASSERT(ok);
    ZEXPECT(*ok == 42);
    ZEXPECT(!parse("x"));
    ZEXPECT(std::optional<int>(1));
}

ZEST_CASE(orderings) {
    ZEXPECT(1 < 2);
    ZEXPECT(1 <= 1);
    ZEXPECT(2 > 1);
    ZEXPECT(2 >= 2);
    ZEXPECT(std::string("alpha") < std::string("beta"));
    ZEXPECT(-1 < 1u);
}

ZEST_CASE(operands_compare_structurally) {
    ZEXPECT(Point{1, 2} == Point{1, 2});
    ZEXPECT(Point{1, 2} != Point{2, 1});
    ZEXPECT(std::vector<Point>{
                {1, 2}
    } == std::vector<Point>{{1, 2}});
    ZEXPECT(std::size_t{3} == 3);
}

ZEST_CASE(char_pointers_compare_as_text_against_text) {
    std::string text = "same";
    std::string copy = text;
    const char* pointer = text.c_str();
    const char* null = nullptr;

    ZEXPECT(pointer == "same");
    ZEXPECT("same" == pointer);
    ZEXPECT(pointer == std::string_view("same"));
    ZEXPECT(pointer != "other");
    ZEXPECT(null != "same");
    // Two pointers compare by address.
    ZEXPECT(pointer != copy.c_str());
    ZEXPECT(pointer == text.c_str());

    // An array's text ends with the array, null character or not.
    const char tag[4] = {'K', 'O', 'T', 'A'};
    const char* kota = "KOTA";
    ZEXPECT(kota == tag);
}

ZEST_CASE(temporaries_outlive_the_check) {
    ZEXPECT(std::string("a") + "b" == "ab");
    ZEXPECT(parse("42").value() == 42);
}

ZEST_CASE(predicates) {
    ZEXPECT(contains(std::string("haystack"), "st"));
    ZEXPECT(contains(std::string_view("haystack"), 'k'));
    ZEXPECT(!contains(std::string("haystack"), "needle"));
    ZEXPECT(contains(std::vector<int>{1, 2, 3}, 2));
    ZEXPECT(!contains(std::vector<int>{1, 2, 3}, 4));
    std::string owned = "b";
    ZEXPECT(contains(std::vector<const char*>{"a", owned.c_str()}, "b"));
    ZEXPECT(starts_with(std::string("prefix-body"), "prefix"));
    ZEXPECT(ends_with(std::string("body-suffix"), "suffix"));
    // A null pattern is no text; an array's text ends with the array.
    const char* null = nullptr;
    const char tail[2] = {'x', 't'};
    ZEXPECT(!contains(std::string("text"), null));
    ZEXPECT(!starts_with(std::string("text"), null));
    ZEXPECT(ends_with(std::string("text"), tail));
    ZEXPECT(type_eq<int, int>());
    ZEXPECT(!type_eq<int, long>());
}

#ifdef __cpp_exceptions

ZEST_CASE(throws_matches_what_is_thrown) {
    auto boom = [] {
        throw std::runtime_error("boom");
    };
    ZEXPECT(throws(boom));
    ZEXPECT(throws<std::runtime_error>(boom));
    ZEXPECT(throws<std::exception>(boom));
    ZEXPECT(!throws<std::logic_error>(boom));
    ZEXPECT(throws([] { throw 42; }));
    ZEXPECT(!throws([] {}));
}

ZEST_CASE(throws_explains_what_was_thrown) {
    auto boom = [] {
        throw std::runtime_error("boom");
    };
    auto wrong = throws<std::logic_error>(boom).explain();
    ZEXPECT(starts_with(wrong, "expected: "));
    ZEXPECT(contains(wrong, "logic_error"));
    ZEXPECT(contains(wrong, "\nthrown: "));
    ZEXPECT(ends_with(throws(boom).explain(), ": boom"));
    ZEXPECT(starts_with(throws([] { throw 42; }).explain(), "thrown: "));
    ZEXPECT(throws([] {}).explain() == "nothing was thrown");
}

#endif

// Only a failed ZASSERT runs a hook; the runner check covers that.
ZEST_CASE(fatal_hooks_do_not_run_on_their_own) {
    bool ran = false;
    {
        FatalHook hook{[&] { ran = true; }};
    }
    ZEXPECT(!ran);
}

ZEST_CASE(negation_keeps_the_explanation) {
    std::string text = "abc";
    auto match = contains(text, "x");
    auto negated = !match;
    ZEXPECT(negated.held);
    ZEXPECT(negated.explain() == match.explain());
}

ZEST_CASE(static_checks) {
    constexpr int answer = 42;
    ZSTATIC_EXPECT(answer == 42);
    ZSTATIC_EXPECT(sizeof(int) >= 2);
    ZSTATIC_EXPECT(std::is_integral_v<int>);
}

ZEST_CASE(contexts_nest_and_unwind) {
    ZEST_CONTEXT("outer {}", 1);
    {
        ZEST_CONTEXT("inner");
        ZEXPECT(1 == 1);
    }
    ZEXPECT(2 == 2);
}

};  // ZEST_SUITE(zest_check)

}  // namespace

}  // namespace kota::zest
