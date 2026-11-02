// The std::optional wrappers of ts.h: assigned, constructed and compared as
// std::optional is, for a T of each kind a protocol field has.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "kotatsu/zest/zest.h"
#include "kotatsu/ipc/lsp/protocol.h"

namespace kotatsu::ipc::protocol {

namespace {

enum class Severity : std::uint8_t { Error = 1, Warning = 2 };

struct Point {
    int line = 0;
    int column = 0;

    bool operator==(const Point&) const = default;
};

/// Assigns, constructs and compares wrapper `W` of a T, given two values of
/// it; the comparisons are the wrapper's own.
template <template <typename> typename W, typename T>
void check_wrapper(std::string_view kind, T first, T second) {
    ZEST_CONTEXT("{}", kind);
    W<T> wrapper;
    wrapper = first;
    ZASSERT(wrapper.has_value());
    ZEXPECT(*wrapper == first);
    const T lvalue = second;
    wrapper = lvalue;
    ZASSERT(wrapper.has_value());
    ZEXPECT(*wrapper == second);
    wrapper = {};
    ZEXPECT(!wrapper.has_value());
    wrapper = std::optional<T>(first);
    ZASSERT(wrapper.has_value());
    ZEXPECT(*wrapper == first);
    wrapper = std::nullopt;
    ZEXPECT(!wrapper.has_value());

    W<T> constructed = first;
    W<T> braced{second};
    W<T> copied = constructed;
    copied = braced;
    ZASSERT(copied.has_value());
    ZEXPECT(*copied == second);
    copied = std::move(constructed);
    ZASSERT(copied.has_value());
    ZEXPECT(*copied == first);

    ZEXPECT((copied == first));
    ZEXPECT((copied != second));
    ZEXPECT((first == copied));
    ZEXPECT((copied == W<T>(first)));
    ZEXPECT((copied != W<T>()));
    ZEXPECT((W<T>() == std::nullopt));
}

template <template <typename> typename W>
void check_every_kind() {
    check_wrapper<W>("bool", true, false);
    check_wrapper<W, integer>("integer", 1, 2);
    check_wrapper<W, uinteger>("uinteger", 1U, 2U);
    check_wrapper<W, decimal>("decimal", 0.5, 1.5);
    check_wrapper<W>("enum", Severity::Error, Severity::Warning);
    check_wrapper<W, string>("string", "a", "b");
    check_wrapper<W>("struct", Point{.line = 1}, Point{.column = 2});
}

ZEST_SUITE(ipc_lsp_ts) {

ZEST_CASE(nullable_is_assigned_as_std_optional) {
    check_every_kind<nullable>();
}

ZEST_CASE(optional_is_assigned_as_std_optional) {
    check_every_kind<optional>();
}

ZEST_CASE(optional_nullable_is_assigned_as_std_optional) {
    check_every_kind<optional_nullable>();
}

ZEST_CASE(assigned_value_converts_to_t) {
    optional<string> text;
    text = "text";
    ZASSERT(text.has_value());
    ZEXPECT(*text == "text");
    optional<decimal> number;
    number = 1;
    ZASSERT(number.has_value());
    ZEXPECT(*number == 1.0);
    optional<Point> point;
    point = {.line = 3, .column = 4};
    ZASSERT(point.has_value());
    ZEXPECT(*point == (Point{.line = 3, .column = 4}));
}

ZEST_CASE(optional_of_another_t_converts_on_assignment) {
    optional<decimal> number;
    number = std::optional<int>(1);
    ZASSERT(number.has_value());
    ZEXPECT(*number == 1.0);
    number = optional<integer>(2);
    ZASSERT(number.has_value());
    ZEXPECT(*number == 2.0);
    // A T that converts only explicitly, as std::optional assigns it.
    optional<string> text;
    text = std::optional<std::string_view>("view");
    ZASSERT(text.has_value());
    ZEXPECT(*text == "view");
}

ZEST_CASE(wrappers_of_one_t_assign_each_other) {
    optional<integer> value = 1;
    nullable<integer> null;
    value = null;
    ZEXPECT(!value.has_value());
    null = optional<integer>(2);
    ZASSERT(null.has_value());
    ZEXPECT(*null == 2);
}

ZEST_CASE(protocol_field_takes_its_enum) {
    Diagnostic diagnostic;
    diagnostic.severity = DiagnosticSeverity::Warning;
    ZEXPECT((diagnostic.severity == DiagnosticSeverity::Warning));
}

// `undefined | null | T`: a value, a present null, or absent.
ZEST_CASE(protocol_field_of_a_nullable_takes_each_state) {
    SignatureHelp help;
    help.active_parameter = 2U;
    ZASSERT(help.active_parameter.has_value());
    ZEXPECT(help.active_parameter->has_value());
    ZEXPECT((*help.active_parameter == 2U));
    help.active_parameter = nullable<uinteger>();
    ZASSERT(help.active_parameter.has_value());
    ZEXPECT(!help.active_parameter->has_value());
    help.active_parameter = std::nullopt;
    ZEXPECT(!help.active_parameter.has_value());
    help.active_parameter = 3U;
    help.active_parameter = {};
    ZEXPECT(!help.active_parameter.has_value());
}

};  // ZEST_SUITE(ipc_lsp_ts)

}  // namespace

}  // namespace kotatsu::ipc::protocol
