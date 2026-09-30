#include <expected>
#include <memory>
#include <string>

#include "kota/zest/zest.h"
#include "kota/support/expected_try.h"

namespace kota {

namespace {

std::expected<void, std::string> check(bool ok) {
    if(!ok) {
        return std::unexpected(std::string("check failed"));
    }
    return {};
}

std::expected<std::unique_ptr<int>, std::string> make(int value) {
    if(value < 0) {
        return std::unexpected(std::string("negative"));
    }
    return std::make_unique<int>(value);
}

std::expected<int, std::string> checked_twice(bool first, bool second, int& reached) {
    KOTA_EXPECTED_TRY(check(first));
    reached = 1;
    KOTA_EXPECTED_TRY(check(second));
    reached = 2;
    return 2;
}

std::expected<int, std::string> sum(int a, int b) {
    KOTA_EXPECTED_TRY_V(auto first, make(a));
    KOTA_EXPECTED_TRY_V(auto second, make(b));
    return *first + *second;
}

std::expected<int, std::string> into_existing(int value) {
    std::unique_ptr<int> owner;
    KOTA_EXPECTED_TRY_V(owner, make(value));
    return *owner;
}

ZEST_SUITE(support_expected_try) {

ZEST_CASE(try_goes_on_after_success) {
    int reached = 0;
    auto result = checked_twice(true, true, reached);
    ASSERT(result.has_value());
    EXPECT(*result == 2);
    EXPECT(reached == 2);
}

ZEST_CASE(try_of_an_error_fails) {
    int reached = 0;
    auto result = checked_twice(true, false, reached);
    ASSERT(!result.has_value());
    EXPECT(result.error() == "check failed");
    EXPECT(reached == 1);

    reached = 0;
    EXPECT(!checked_twice(false, true, reached).has_value());
    EXPECT(reached == 0);
}

ZEST_CASE(try_v_declares_the_value) {
    auto result = sum(2, 3);
    ASSERT(result.has_value());
    EXPECT(*result == 5);
}

ZEST_CASE(try_v_of_an_error_fails) {
    auto result = sum(2, -1);
    ASSERT(!result.has_value());
    EXPECT(result.error() == "negative");
}

ZEST_CASE(try_v_assigns_an_existing_variable) {
    auto result = into_existing(7);
    ASSERT(result.has_value());
    EXPECT(*result == 7);
    auto failed = into_existing(-7);
    ASSERT(!failed.has_value());
    EXPECT(failed.error() == "negative");
}

};  // ZEST_SUITE(support_expected_try)

}  // namespace

}  // namespace kota
