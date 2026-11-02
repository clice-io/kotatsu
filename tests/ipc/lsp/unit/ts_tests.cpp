// The std::optional wrappers of ts.h: assigned, constructed and compared as
// std::optional is, for a T of each kind a protocol field has.

#include <cstdint>
#include <optional>
#include <string>
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
void check_wrapper(T first, T second) {
    W<T> wrapper;
    wrapper = first;
    ZASSERT(wrapper.has_value());
    ZEXPECT(*wrapper == first);
    const T lvalue = second;
    wrapper = lvalue;
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
    check_wrapper<W>(true, false);
    check_wrapper<W, integer>(1, 2);
    check_wrapper<W, uinteger>(1U, 2U);
    check_wrapper<W, decimal>(0.5, 1.5);
    check_wrapper<W>(Severity::Error, Severity::Warning);
    check_wrapper<W, string>("a", "b");
    check_wrapper<W>(Point{.line = 1}, Point{.column = 2});
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

};  // ZEST_SUITE(ipc_lsp_ts)

}  // namespace

}  // namespace kotatsu::ipc::protocol
