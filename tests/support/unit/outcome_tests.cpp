#include <concepts>
#include <string>
#include <string_view>
#include <utility>

#include "support/harness/throws.h"
#include "kotatsu/zest/macro.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/support/config.h"
#include "kotatsu/support/outcome.h"

namespace kotatsu {

namespace {

/// An error that says what went wrong, as kotatsu::async::error does.
struct Failure {
    int code = 0;

    std::string_view message() const {
        return "operation failed";
    }
};

struct Cancelled {};

using Full = outcome<int, Failure, Cancelled>;

struct Plain {
    int code = 0;
};

ZEST_SUITE(support_outcome) {

ZEST_CASE(value_is_the_ok_state) {
    Full o = 42;
    ZASSERT(o.has_value());
    ZEXPECT(!o.has_error());
    ZEXPECT(!o.is_cancelled());
    ZEXPECT(static_cast<bool>(o));
    ZEXPECT(o.value() == 42);
    ZEXPECT(*o == 42);
}

ZEST_CASE(error_is_the_err_state) {
    Full o = outcome_error(Failure{.code = 1});
    ZASSERT(o.has_error());
    ZEXPECT(!o.has_value());
    ZEXPECT(!o.is_cancelled());
    ZEXPECT(!static_cast<bool>(o));
    ZEXPECT(o.error().code == 1);
}

ZEST_CASE(cancellation_is_the_cancelled_state) {
    Full o = outcome_cancel(Cancelled{});
    ZEXPECT(o.is_cancelled());
    ZEXPECT(!o.has_value());
    ZEXPECT(!o.has_error());
}

ZEST_CASE(void_value_is_ok) {
    outcome<void, Failure, Cancelled> defaulted;
    ZEXPECT(defaulted.has_value());

    outcome<void, Failure, Cancelled> tagged = outcome_value();
    ZEXPECT(tagged.has_value());
}

ZEST_CASE(arrow_reaches_into_the_value) {
    outcome<std::string, Failure> o = std::string("kotatsu");
    ZASSERT(o.has_value());
    ZEXPECT(o->size() == 7U);

    const auto& constant = o;
    ZEXPECT(constant->front() == 'k');
    ZEXPECT(*constant == "kotatsu");
}

ZEST_CASE(accessors_follow_the_value_category) {
    outcome<std::string, Failure, Cancelled> value = std::string("payload");
    const auto& constant = value;
    ZASSERT(value.has_value());
    ZEXPECT(zest::type_eq<decltype(value.value()), std::string&>());
    ZEXPECT(zest::type_eq<decltype(constant.value()), const std::string&>());
    ZEXPECT(zest::type_eq<decltype(*constant), const std::string&>());
    ZEXPECT(zest::type_eq<decltype(std::move(value).value()), std::string&&>());
    ZEXPECT(zest::type_eq<decltype(*std::move(value)), std::string&&>());
    std::string taken = std::move(value).value();
    ZEXPECT(taken == "payload");

    outcome<std::string, Failure, Cancelled> failed = outcome_error(Failure{.code = 2});
    ZASSERT(failed.has_error());
    ZEXPECT(zest::type_eq<decltype(std::move(failed).error()), Failure&&>());
    ZEXPECT(std::move(failed).error().code == 2);

    outcome<std::string, Failure, Cancelled> cancelled = outcome_cancel(Cancelled{});
    ZASSERT(cancelled.is_cancelled());
    ZEXPECT(zest::type_eq<decltype(std::move(cancelled).cancellation()), Cancelled&&>());
}

ZEST_CASE(outcome_without_channels_always_holds_a_value) {
    outcome<int> number = 3;
    ZASSERT(number.has_value());
    ZEXPECT(static_cast<bool>(number));
    ZEXPECT(*number == 3);

    outcome<void> nothing;
    ZEXPECT(nothing.has_value());
}

ZEST_CASE(channel_types_are_exposed) {
    ZEXPECT(zest::type_eq<Full::value_type, int>());
    ZEXPECT(zest::type_eq<Full::error_type, Failure>());
    ZEXPECT(zest::type_eq<Full::cancel_type, Cancelled>());
    ZSTATIC_EXPECT(is_outcome_v<Full>);
    ZSTATIC_EXPECT(!is_outcome_v<int>);
}

ZEST_CASE(value_is_not_converted_from_another_outcome) {
    // An outcome's explicit operator bool would otherwise build a bool value.
    ZSTATIC_EXPECT(!std::constructible_from<outcome<bool, Failure>, outcome<int, Failure>>);
    // An outcome of exactly that type is a value like any other.
    ZSTATIC_EXPECT(
        std::constructible_from<outcome<outcome<int, Failure>, Failure>, outcome<int, Failure>>);
}

ZEST_CASE(unwrap_gives_the_value_in_its_value_category) {
    outcome<std::string, Failure, Cancelled> value = std::string("payload");
    const auto& constant = value;
    ZEXPECT(zest::type_eq<decltype(value.unwrap()), std::string&>());
    ZEXPECT(zest::type_eq<decltype(constant.unwrap()), const std::string&>());
    ZEXPECT(zest::type_eq<decltype(std::move(value).unwrap()), std::string&&>());
    ZEXPECT(value.unwrap() == "payload");
    std::string taken = std::move(value).unwrap();
    ZEXPECT(taken == "payload");

    outcome<void, Failure> nothing;
    ZEXPECT(zest::type_eq<decltype(nothing.unwrap()), void>());
    nothing.unwrap();

    outcome<int> plain = 3;
    ZEXPECT(plain.unwrap() == 3);
}

#if KOTATSU_ENABLE_EXCEPTIONS

ZEST_CASE(unwrap_of_an_error_fails) {
    outcome<int, Failure> failed = outcome_error(Failure{.code = 1});
    ZEXPECT(test::thrown<bad_outcome_access>([&] { failed.unwrap(); }) == "operation failed");

    outcome<void, Plain> plain = outcome_error(Plain{.code = 7});
    ZEXPECT(test::thrown<bad_outcome_access>([&] { plain.unwrap(); }) == "outcome holds an error");
}

ZEST_CASE(unwrap_of_a_cancellation_fails) {
    outcome<int, Failure, Cancelled> cancelled = outcome_cancel(Cancelled{});
    ZEXPECT(test::thrown<bad_outcome_access>([&] { std::move(cancelled).unwrap(); }) ==
            "outcome was cancelled");
}

#endif  // KOTATSU_ENABLE_EXCEPTIONS

};  // ZEST_SUITE(support_outcome)

}  // namespace

}  // namespace kotatsu
