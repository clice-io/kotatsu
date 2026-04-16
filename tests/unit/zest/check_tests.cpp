#include <expected>
#include <optional>
#include <string>

#include "kota/zest/zest.h"

namespace kota::zest {

namespace {

TEST_SUITE(zest_check) {

TEST_CASE(binary_equal_expected_and_expected) {
    std::expected<int, std::string> ok_a = 42;
    std::expected<int, std::string> ok_b = 42;
    std::expected<int, std::string> ok_c = 7;
    std::expected<int, std::string> err_a = std::unexpected(std::string("boom"));
    std::expected<int, std::string> err_b = std::unexpected(std::string("boom"));
    std::expected<int, std::string> err_c = std::unexpected(std::string("oops"));

    EXPECT_EQ(ok_a, ok_b);
    EXPECT_NE(ok_a, ok_c);
    EXPECT_NE(ok_a, err_a);

    EXPECT_EQ(err_a, err_b);
    EXPECT_NE(err_a, err_c);
}

TEST_CASE(binary_equal_expected_and_plain) {
    std::expected<int, std::string> ok = 42;
    std::expected<int, std::string> err = std::unexpected(std::string("boom"));

    EXPECT_EQ(ok, 42);
    EXPECT_EQ(42, ok);
    EXPECT_NE(ok, 7);
    EXPECT_NE(7, ok);
    EXPECT_NE(err, 42);
    EXPECT_NE(42, err);
}

TEST_CASE(binary_equal_optional_and_plain) {
    std::optional<int> some = 42;
    std::optional<int> none = std::nullopt;

    EXPECT_EQ(some, 42);
    EXPECT_EQ(42, some);
    EXPECT_NE(some, 7);
    EXPECT_NE(7, some);
    EXPECT_NE(none, 42);
    EXPECT_NE(42, none);
}

TEST_CASE(binary_equal_optional_and_optional) {
    std::optional<int> some_a = 42;
    std::optional<int> some_b = 42;
    std::optional<int> some_c = 7;
    std::optional<int> none_a = std::nullopt;
    std::optional<int> none_b = std::nullopt;

    EXPECT_EQ(some_a, some_b);
    EXPECT_NE(some_a, some_c);
    EXPECT_NE(some_a, none_a);
    EXPECT_NE(none_a, some_a);
    EXPECT_EQ(none_a, none_b);
}

TEST_CASE(binary_equal_expected_and_optional) {
    std::expected<int, std::string> exp_ok_42 = 42;
    std::expected<int, std::string> exp_ok_7 = 7;
    std::expected<int, std::string> exp_err = std::unexpected(std::string("boom"));

    std::optional<int> some_42 = 42;
    std::optional<int> some_7 = 7;
    std::optional<int> none = std::nullopt;

    EXPECT_EQ(exp_ok_42, some_42);
    EXPECT_EQ(some_42, exp_ok_42);
    EXPECT_NE(exp_ok_42, some_7);
    EXPECT_NE(some_7, exp_ok_42);
    EXPECT_NE(exp_ok_7, some_42);
    EXPECT_NE(some_42, exp_ok_7);

    EXPECT_NE(exp_ok_42, none);
    EXPECT_NE(none, exp_ok_42);

    EXPECT_NE(exp_err, some_42);
    EXPECT_NE(some_42, exp_err);
    EXPECT_NE(exp_err, none);
    EXPECT_NE(none, exp_err);
}

TEST_CASE(binary_ordering_expect_macros) {
    EXPECT_LT(1, 2);
    EXPECT_LE(1, 1);
    EXPECT_LE(1, 2);
    EXPECT_GT(2, 1);
    EXPECT_GE(2, 2);
    EXPECT_GE(2, 1);

    std::string a = "alpha";
    std::string b = "beta";
    EXPECT_LT(a, b);
    EXPECT_GT(b, a);
}

TEST_CASE(binary_ordering_assert_macros) {
    ASSERT_LT(3, 4);
    ASSERT_LE(3, 3);
    ASSERT_LE(3, 4);
    ASSERT_GT(4, 3);
    ASSERT_GE(4, 4);
    ASSERT_GE(4, 3);
}

};  // TEST_SUITE(zest_check)

}  // namespace

}  // namespace kota::zest
