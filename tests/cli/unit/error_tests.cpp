// An error rendered under its argv, the elements at fault marked.

#include <string>
#include <vector>

#include "kotatsu/cli/error.h"
#include "kotatsu/zest/zest.h"

namespace kotatsu::cli {

namespace {

Error at(std::vector<std::string> argv, std::uint32_t begin, std::uint32_t end) {
    return Error{.kind = Error::Kind::InvalidValue,
                 .message = "bad",
                 .argv = std::move(argv),
                 .begin = begin,
                 .end = end};
}

ZEST_SUITE(cli_error) {

ZEST_CASE(argument_is_marked) {
    ZEXPECT(at({"--jobs", "many"}, 1, 2).render() ==
            "at argv[1]:\n  --jobs many\n         ^~~~\n  bad");
}

ZEST_CASE(several_arguments_are_marked_as_one) {
    ZEXPECT(at({"-o", "a", "b"}, 0, 2).render() == "at argv[0]:\n  -o a b\n  ^~~~\n  bad");
}

ZEST_CASE(empty_range_marks_its_first_argument) {
    ZEXPECT(at({"-o", "a"}, 1, 1).render() == "at argv[1]:\n  -o a\n     ^\n  bad");
}

ZEST_CASE(end_of_argv_is_marked_past_it) {
    ZEXPECT(at({"core"}, 1, 1).render() == "at end of argv:\n  core\n      ^\n  bad");
}

ZEST_CASE(empty_argv_is_the_message_alone) {
    ZEXPECT(at({}, 0, 0).render() == "bad");
}

ZEST_CASE(empty_argument_is_marked_where_it_stands) {
    ZEXPECT(at({"compute", ""}, 1, 2).render() == "at argv[1]:\n  compute \n          ^\n  bad");
}

ZEST_CASE(long_line_is_cut_around_the_mark) {
    std::vector<std::string> argv(30, "word");
    const auto rendered = at(argv, 20, 21).render(Style{.argv_width = 40});
    ZEXPECT(rendered.starts_with("at argv[20]:\n  ..."));
    ZEXPECT(rendered.contains("..."));
}

ZEST_CASE(zero_width_keeps_the_whole_line) {
    std::vector<std::string> argv(30, "word");
    const auto rendered = at(argv, 20, 21).render(Style{.argv_width = 0});
    ZEXPECT(!rendered.contains("..."));
}

ZEST_CASE(color_look_draws_the_mark) {
    const auto rendered = at({"--jobs", "many"}, 1, 2).render(Style{.color = true});
    ZEXPECT(rendered.contains("[argv[1]]"));
    ZEXPECT(rendered.contains("╰─▶"));
}

};  // ZEST_SUITE(cli_error)

}  // namespace

}  // namespace kotatsu::cli
