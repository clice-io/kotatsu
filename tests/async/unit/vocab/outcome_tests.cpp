#include <concepts>
#include <string>
#include <utility>

#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/vocab/error.h"
#include "kota/async/vocab/outcome.h"

namespace kota {

namespace {

using Full = outcome<int, error, cancellation>;

ZEST_SUITE(async_vocab_outcome) {

ZEST_CASE(value_is_the_ok_state) {
    Full o = 42;
    EXPECT(o.state() == Full::State::ok);
    EXPECT(o.has_value());
    EXPECT(!o.has_error());
    EXPECT(!o.is_cancelled());
    EXPECT(static_cast<bool>(o));
    EXPECT(o.value() == 42);
    EXPECT(*o == 42);
}

ZEST_CASE(error_is_the_err_state) {
    Full o = outcome_error(error::invalid_argument);
    EXPECT(o.state() == Full::State::err);
    EXPECT(o.has_error());
    EXPECT(!o.has_value());
    EXPECT(!o.is_cancelled());
    EXPECT(!static_cast<bool>(o));
    EXPECT(o.error() == error::invalid_argument);
}

ZEST_CASE(cancellation_is_the_cancelled_state) {
    Full o = outcome_cancel(cancellation("shutting down"));
    EXPECT(o.state() == Full::State::cancelled);
    EXPECT(o.is_cancelled());
    EXPECT(!o.has_value());
    EXPECT(!o.has_error());
    EXPECT(o.cancellation().reason() == "shutting down");
}

ZEST_CASE(void_value_is_ok) {
    outcome<void, error, cancellation> defaulted;
    EXPECT(defaulted.has_value());

    outcome<void, error, cancellation> tagged = outcome_value();
    EXPECT(tagged.has_value());
}

ZEST_CASE(arrow_reaches_into_the_value) {
    outcome<std::string, error> o = std::string("kotatsu");
    EXPECT(o->size() == 7U);

    const auto& constant = o;
    EXPECT(constant->front() == 'k');
    EXPECT(*constant == "kotatsu");
}

ZEST_CASE(rvalue_accessors_move_out) {
    outcome<std::string, error, cancellation> value = std::string("payload");
    EXPECT(zest::type_eq<decltype(std::move(value).value()), std::string&&>());
    std::string taken = std::move(value).value();
    EXPECT(taken == "payload");

    outcome<std::string, error, cancellation> failed = outcome_error(error::io_error);
    EXPECT(zest::type_eq<decltype(std::move(failed).error()), error&&>());
    EXPECT(std::move(failed).error() == error::io_error);

    outcome<std::string, error, cancellation> cancelled = outcome_cancel(cancellation("stop"));
    cancellation why = std::move(cancelled).cancellation();
    EXPECT(why.reason() == "stop");
}

ZEST_CASE(outcome_without_channels_always_holds_a_value) {
    outcome<int> number = 3;
    EXPECT(number.has_value());
    EXPECT(static_cast<bool>(number));
    EXPECT(*number == 3);

    outcome<void> nothing;
    EXPECT(nothing.has_value());
}

ZEST_CASE(channel_types_are_exposed) {
    EXPECT(zest::type_eq<Full::value_type, int>());
    EXPECT(zest::type_eq<Full::error_type, error>());
    EXPECT(zest::type_eq<Full::cancel_type, cancellation>());
    EXPECT(zest::type_eq<result<int>, outcome<int, error, void>>());
    STATIC_EXPECT(is_outcome_v<result<int>>);
    STATIC_EXPECT(!is_outcome_v<int>);
}

ZEST_CASE(value_is_not_converted_from_another_outcome) {
    // An outcome's explicit operator bool would otherwise build a bool value.
    STATIC_EXPECT(!std::constructible_from<outcome<bool, error>, outcome<int, error>>);
    // An outcome of exactly that type is a value like any other.
    STATIC_EXPECT(std::constructible_from<outcome<result<int>, error>, result<int>>);
}

};  // ZEST_SUITE(async_vocab_outcome)

}  // namespace

}  // namespace kota
