#include <concepts>
#include <string>
#include <utility>

#include "async/harness/exceptions.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/support/config.h"
#include "kota/async/vocab/error.h"
#include "kota/async/vocab/outcome.h"

namespace kota {

namespace {

using Full = outcome<int, error, cancellation>;

struct Plain {
    int code = 0;
};

ZEST_SUITE(async_vocab_outcome) {

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
    Full o = outcome_error(error::invalid_argument);
    ZASSERT(o.has_error());
    ZEXPECT(!o.has_value());
    ZEXPECT(!o.is_cancelled());
    ZEXPECT(!static_cast<bool>(o));
    ZEXPECT(o.error() == error::invalid_argument);
}

ZEST_CASE(cancellation_is_the_cancelled_state) {
    Full o = outcome_cancel(cancellation{});
    ZEXPECT(o.is_cancelled());
    ZEXPECT(!o.has_value());
    ZEXPECT(!o.has_error());
}

ZEST_CASE(void_value_is_ok) {
    outcome<void, error, cancellation> defaulted;
    ZEXPECT(defaulted.has_value());

    outcome<void, error, cancellation> tagged = outcome_value();
    ZEXPECT(tagged.has_value());
}

ZEST_CASE(arrow_reaches_into_the_value) {
    outcome<std::string, error> o = std::string("kotatsu");
    ZASSERT(o.has_value());
    ZEXPECT(o->size() == 7U);

    const auto& constant = o;
    ZEXPECT(constant->front() == 'k');
    ZEXPECT(*constant == "kotatsu");
}

ZEST_CASE(accessors_follow_the_value_category) {
    outcome<std::string, error, cancellation> value = std::string("payload");
    const auto& constant = value;
    ZASSERT(value.has_value());
    ZEXPECT(zest::type_eq<decltype(value.value()), std::string&>());
    ZEXPECT(zest::type_eq<decltype(constant.value()), const std::string&>());
    ZEXPECT(zest::type_eq<decltype(*constant), const std::string&>());
    ZEXPECT(zest::type_eq<decltype(std::move(value).value()), std::string&&>());
    ZEXPECT(zest::type_eq<decltype(*std::move(value)), std::string&&>());
    std::string taken = std::move(value).value();
    ZEXPECT(taken == "payload");

    outcome<std::string, error, cancellation> failed = outcome_error(error::io_error);
    ZASSERT(failed.has_error());
    ZEXPECT(zest::type_eq<decltype(std::move(failed).error()), error&&>());
    ZEXPECT(std::move(failed).error() == error::io_error);

    outcome<std::string, error, cancellation> cancelled = outcome_cancel(cancellation{});
    ZASSERT(cancelled.is_cancelled());
    ZEXPECT(zest::type_eq<decltype(std::move(cancelled).cancellation()), cancellation&&>());
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
    ZEXPECT(zest::type_eq<Full::error_type, error>());
    ZEXPECT(zest::type_eq<Full::cancel_type, cancellation>());
    ZEXPECT(zest::type_eq<result<int>, outcome<int, error, void>>());
    ZSTATIC_EXPECT(is_outcome_v<result<int>>);
    ZSTATIC_EXPECT(!is_outcome_v<int>);
}

ZEST_CASE(value_is_not_converted_from_another_outcome) {
    // An outcome's explicit operator bool would otherwise build a bool value.
    ZSTATIC_EXPECT(!std::constructible_from<outcome<bool, error>, outcome<int, error>>);
    // An outcome of exactly that type is a value like any other.
    ZSTATIC_EXPECT(std::constructible_from<outcome<result<int>, error>, result<int>>);
}

ZEST_CASE(unwrap_gives_the_value_in_its_value_category) {
    outcome<std::string, error, cancellation> value = std::string("payload");
    const auto& constant = value;
    ZEXPECT(zest::type_eq<decltype(value.unwrap()), std::string&>());
    ZEXPECT(zest::type_eq<decltype(constant.unwrap()), const std::string&>());
    ZEXPECT(zest::type_eq<decltype(std::move(value).unwrap()), std::string&&>());
    ZEXPECT(value.unwrap() == "payload");
    std::string taken = std::move(value).unwrap();
    ZEXPECT(taken == "payload");

    outcome<void, error> nothing;
    ZEXPECT(zest::type_eq<decltype(nothing.unwrap()), void>());
    nothing.unwrap();

    outcome<int> plain = 3;
    ZEXPECT(plain.unwrap() == 3);
}

#if KOTA_ENABLE_EXCEPTIONS

// Reads what was thrown; see test::exceptions_unreadable.
ZEST_CASE(unwrap_of_an_error_fails, skip = test::exceptions_unreadable) {
    result<int> failed = outcome_error(error::connection_refused);
    ZEXPECT(test::thrown<bad_outcome_access>([&] { failed.unwrap(); }) ==
            std::string(error::connection_refused.message()));

    outcome<void, Plain> plain = outcome_error(Plain{.code = 7});
    ZEXPECT(test::thrown<bad_outcome_access>([&] { plain.unwrap(); }) == "outcome holds an error");
}

// Reads what was thrown; see test::exceptions_unreadable.
ZEST_CASE(unwrap_of_a_cancellation_fails, skip = test::exceptions_unreadable) {
    outcome<int, error, cancellation> cancelled = outcome_cancel(cancellation{});
    ZEXPECT(test::thrown<bad_outcome_access>([&] { std::move(cancelled).unwrap(); }) ==
            "outcome was cancelled");
}

#endif  // KOTA_ENABLE_EXCEPTIONS

};  // ZEST_SUITE(async_vocab_outcome)

}  // namespace

}  // namespace kota
