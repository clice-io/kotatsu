#include <string>
#include <utility>

#include "kota/zest/zest.h"
#include "kota/codec/visit/context.h"

namespace kota::codec {

namespace {

/// An error as the decoders leave it: a message, then the path segments the
/// frames prepend on the way out, innermost first.
rich_error unwound(std::string message) {
    rich_error error(std::move(message));
    error.prepend_index(3);
    error.prepend_field("b");
    error.prepend_field("a");
    return error;
}

ZEST_SUITE(codec_visit_context) {

ZEST_CASE(error_holds_when_it_has_a_message) {
    ZEXPECT(!rich_error());
    ZEXPECT(static_cast<bool>(rich_error("broken")));
}

ZEST_CASE(path_joins_fields_and_indices) {
    ZEXPECT(unwound("broken").format_path() == "a.b[3]");
}

ZEST_CASE(path_starting_with_an_index) {
    rich_error error("broken");
    error.prepend_field("scores");
    error.prepend_index(2);
    ZEXPECT(error.format_path() == "[2].scores");
}

ZEST_CASE(empty_path_formats_empty) {
    ZEXPECT(rich_error("broken").format_path() == "");
}

ZEST_CASE(to_string_is_the_message_alone) {
    ZEXPECT(rich_error("broken").to_string() == "broken");
}

ZEST_CASE(to_string_adds_the_path) {
    ZEXPECT(unwound("broken").to_string() == "broken at a.b[3]");
}

ZEST_CASE(to_string_adds_the_location) {
    auto error = unwound("broken");
    error.location = {.line = 3, .column = 10, .byte_offset = 30};
    ZEXPECT(error.to_string() == "broken at a.b[3] (line 3, column 10)");
}

ZEST_CASE(to_string_of_no_error_is_empty) {
    auto error = unwound("");
    ZEXPECT(error.to_string() == "");
}

ZEST_CASE(protocol_messages) {
    ZEXPECT(rich_error::missing_field("name").message == "missing required field 'name'");
    ZEXPECT(rich_error::unknown_field("extra").message == "unknown field 'extra'");
    ZEXPECT(rich_error::invalid_type("string", "integer").message ==
            "invalid type: expected string, got integer");
}

ZEST_CASE(scoped_context_nests) {
    ZEXPECT(scoped_context<rich_error>::try_current() == nullptr);
    rich_error outer;
    {
        scoped_context<rich_error> outer_scope(outer);
        rich_error inner;
        {
            scoped_context<rich_error> inner_scope(inner);
            ZEXPECT(scoped_context<rich_error>::try_current() == &inner);
            ZEXPECT(!scoped_context<rich_error>::fail(rich_error("inner")));
        }
        ZEXPECT(scoped_context<rich_error>::try_current() == &outer);
        ZEXPECT(inner.message == "inner");
        ZEXPECT(outer.message == "");
    }
    ZEXPECT(scoped_context<rich_error>::try_current() == nullptr);
}

ZEST_CASE(scoped_context_fail_without_a_scope_drops_the_error) {
    ZEXPECT(!scoped_context<rich_error>::fail(rich_error("dropped")));
}

};  // ZEST_SUITE(codec_visit_context)

}  // namespace

}  // namespace kota::codec
