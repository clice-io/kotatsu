#include <string>
#include <string_view>
#include <vector>

#include "deco/harness/option_table.h"
#include "kota/deco/option.h"
#include "kota/zest/zest.h"

namespace kota::option {

namespace {

using test::kinds_table;
using test::parse_table;
using strings = std::vector<std::string>;

ZEST_SUITE(deco_option_table_parse) {

ZEST_CASE(flag_takes_no_value) {
    const auto parse = parse_table(kinds_table(), "-f");
    ZEXPECT(parse.errors.empty());
    ZASSERT(parse.args.size() == 1U);
    ZEXPECT(parse.args[0].id == test::FlagId);
    ZEXPECT(parse.args[0].spelling == "-f");
    ZEXPECT(parse.args[0].values.empty());
    ZEXPECT(parse.args[0].index == 0U);
    ZEXPECT(parse.args[0].next_index == 1U);
}

ZEST_CASE(flag_followed_by_text_is_unknown) {
    const auto parse = parse_table(kinds_table(), "-fx");
    ZASSERT(parse.args.size() == 1U);
    ZEXPECT(parse.args[0].id == test::UnknownId);
    ZEXPECT(parse.args[0].spelling == "-fx");
}

ZEST_CASE(joined_takes_the_rest_of_its_element) {
    const auto parse = parse_table(kinds_table(), "-jvalue -j");
    ZASSERT(parse.args.size() == 2U);
    ZEXPECT(parse.args[0].id == test::JoinedId);
    ZEXPECT(parse.args[0].spelling == "-j");
    ZEXPECT(parse.values(0) == strings{"value"});
    ZEXPECT(parse.values(1) == strings{""});
}

ZEST_CASE(separate_takes_the_next_element) {
    // Whatever it holds: an option's spelling is a value like any other.
    const auto parse = parse_table(kinds_table(), "-s -f");
    ZEXPECT(parse.errors.empty());
    ZASSERT(parse.args.size() == 1U);
    ZEXPECT(parse.args[0].id == test::SeparateId);
    ZEXPECT(parse.values(0) == strings{"-f"});
    ZEXPECT(parse.args[0].next_index == 2U);
}

ZEST_CASE(separate_without_value_fails) {
    const auto parse = parse_table(kinds_table(), "-f -s");
    ZASSERT(parse.args.size() == 1U);
    ZASSERT(parse.errors.size() == 1U);
    ZEXPECT(parse.errors[0].after == 1U);
    ZEXPECT(parse.errors[0].error.index == 1U);
    ZEXPECT(std::string_view(parse.errors[0].error.message) == "missing argument value");
}

ZEST_CASE(separate_given_an_empty_value_fails) {
    // An empty element reads as no element, as a null one does to LLVM, whose tables these
    // are; parsing goes on after it.
    const auto parse = parse_table(kinds_table(), test::args("-s", "", "-f"));
    ZASSERT(parse.errors.size() == 1U);
    ZEXPECT(parse.errors[0].error.index == 0U);
    ZEXPECT(std::string_view(parse.errors[0].error.message) == "missing argument value");
    ZASSERT(parse.args.size() == 1U);
    ZEXPECT(parse.args[0].id == test::FlagId);
}

ZEST_CASE(empty_elements_are_skipped) {
    const auto parse = parse_table(kinds_table(), test::args("", "-f", "", "main.cc"));
    ZEXPECT(parse.errors.empty());
    ZASSERT(parse.args.size() == 2U);
    ZEXPECT(parse.args[0].index == 1U);
    ZEXPECT(parse.args[1].index == 3U);
}

ZEST_CASE(comma_joined_splits_at_commas) {
    const auto parse = parse_table(kinds_table(), "-Wl,a,,b -Wl,");
    ZASSERT(parse.args.size() == 2U);
    ZEXPECT(parse.args[0].id == test::CommaJoinedId);
    ZEXPECT(parse.values(0) == (strings{"a", "b"}));
    ZEXPECT(parse.values(1).empty());
}

ZEST_CASE(multi_arg_takes_its_count) {
    const auto parse = parse_table(kinds_table(), "--pair a b c");
    ZASSERT(parse.args.size() == 2U);
    ZEXPECT(parse.args[0].id == test::MultiArgId);
    ZEXPECT(parse.values(0) == (strings{"a", "b"}));
    ZEXPECT(parse.args[1].id == test::InputId);
    ZEXPECT(parse.args[1].spelling == "c");
}

ZEST_CASE(multi_arg_short_of_values_fails) {
    const auto parse = parse_table(kinds_table(), "--pair a");
    ZEXPECT(parse.args.empty());
    ZASSERT(parse.errors.size() == 1U);
    ZEXPECT(parse.errors[0].error.index == 0U);
    ZEXPECT(std::string_view(parse.errors[0].error.message) == "missing argument value");
}

ZEST_CASE(joined_or_separate_takes_either) {
    const auto parse = parse_table(kinds_table(), "-ovalue -o next");
    ZEXPECT(parse.errors.empty());
    ZASSERT(parse.args.size() == 2U);
    ZEXPECT(parse.args[0].id == test::JoinedOrSeparateId);
    ZEXPECT(parse.values(0) == strings{"value"});
    ZEXPECT(parse.args[1].id == test::JoinedOrSeparateId);
    ZEXPECT(parse.values(1) == strings{"next"});
}

ZEST_CASE(joined_or_separate_without_value_fails) {
    const auto parse = parse_table(kinds_table(), "-o");
    ZEXPECT(parse.args.empty());
    ZASSERT(parse.errors.size() == 1U);
    ZEXPECT(parse.errors[0].error.index == 0U);
    ZEXPECT(std::string_view(parse.errors[0].error.message) == "missing argument value");
}

ZEST_CASE(joined_and_separate_takes_both) {
    const auto parse = parse_table(kinds_table(), "-xa b -x c");
    ZEXPECT(parse.errors.empty());
    ZASSERT(parse.args.size() == 2U);
    ZEXPECT(parse.args[0].id == test::JoinedAndSeparateId);
    ZEXPECT(parse.values(0) == (strings{"a", "b"}));
    ZEXPECT(parse.values(1) == (strings{"", "c"}));
}

ZEST_CASE(joined_and_separate_without_second_value_fails) {
    const auto parse = parse_table(kinds_table(), "-xa");
    ZEXPECT(parse.args.empty());
    ZASSERT(parse.errors.size() == 1U);
    ZEXPECT(parse.errors[0].error.index == 0U);
    ZEXPECT(std::string_view(parse.errors[0].error.message) == "missing argument value");
}

ZEST_CASE(remaining_args_take_everything_after) {
    const auto parse = parse_table(kinds_table(), "--rest a -f --");
    ZASSERT(parse.args.size() == 1U);
    ZEXPECT(parse.args[0].id == test::RemainingId);
    ZEXPECT(parse.values(0) == (strings{"a", "-f", "--"}));
    ZEXPECT(parse.args[0].next_index == 4U);
}

ZEST_CASE(remaining_args_stop_at_an_empty_element) {
    const auto parse = parse_table(kinds_table(), test::args("--rest", "a", "", "-f"));
    ZASSERT(parse.args.size() == 2U);
    ZEXPECT(parse.values(0) == strings{"a"});
    ZEXPECT(parse.args[1].id == test::FlagId);
}

ZEST_CASE(remaining_args_joined_take_the_rest_too) {
    const auto parse = parse_table(kinds_table(), "--tailz a --tail b");
    ZASSERT(parse.args.size() == 1U);
    ZEXPECT(parse.args[0].id == test::RemainingJoinedId);
    ZEXPECT(parse.values(0) == (strings{"z", "a", "--tail", "b"}));
}

ZEST_CASE(remaining_args_joined_may_start_separate) {
    const auto parse = parse_table(kinds_table(), "--tail b");
    ZASSERT(parse.args.size() == 1U);
    ZEXPECT(parse.values(0) == strings{"b"});
}

ZEST_CASE(longest_spelling_wins) {
    const auto parse = parse_table(kinds_table(), "-jx -jxy");
    ZASSERT(parse.args.size() == 2U);
    ZEXPECT(parse.args[0].id == test::JxId);
    ZEXPECT(parse.args[1].id == test::JoinedId);
    ZEXPECT(parse.values(1) == strings{"xy"});
}

ZEST_CASE(input_is_what_no_prefix_starts) {
    const auto parse = parse_table(kinds_table(), "main.cc - x-y");
    ZASSERT(parse.args.size() == 3U);
    for(std::size_t i = 0; i < parse.args.size(); ++i) {
        ZEST_CONTEXT("argument {}", i);
        ZEXPECT(parse.args[i].id == test::InputId);
        ZEXPECT(parse.args[i].spelling == parse.argv[i]);
        ZEXPECT(parse.args[i].values.empty());
    }
}

ZEST_CASE(slash_that_names_no_option_is_input) {
    // "/" is a prefix of the table, which a path like this also starts with.
    const auto parse = parse_table(kinds_table(), "/usr/bin/cc /Foout.o");
    ZASSERT(parse.args.size() == 2U);
    ZEXPECT(parse.args[0].id == test::InputId);
    ZEXPECT(parse.args[0].spelling == "/usr/bin/cc");
    ZEXPECT(parse.args[1].id == test::SlashId);
    ZEXPECT(parse.values(1) == strings{"out.o"});
}

ZEST_CASE(any_prefix_of_an_option_names_it) {
    const auto parse = parse_table(kinds_table(), "-Foout.o");
    ZASSERT(parse.args.size() == 1U);
    ZEXPECT(parse.args[0].id == test::SlashId);
    ZEXPECT(parse.args[0].spelling == "-Fo");
}

ZEST_CASE(unknown_option_stands_alone) {
    const auto parse = parse_table(kinds_table(), "--nope a");
    ZASSERT(parse.args.size() == 2U);
    ZEXPECT(parse.args[0].id == test::UnknownId);
    ZEXPECT(parse.args[0].spelling == "--nope");
    ZEXPECT(parse.args[0].values.empty());
    ZEXPECT(parse.args[1].id == test::InputId);
}

ZEST_CASE(greedy_unknown_takes_values_up_to_an_option) {
    const auto parse = parse_table(kinds_table(), "--nope a -fx b -jc", {.greedy_unknown = true});
    ZASSERT(parse.args.size() == 2U);
    ZEXPECT(parse.args[0].id == test::UnknownId);
    // -fx names no option: -f is a flag, which takes nothing joined.
    ZEXPECT(parse.values(0) == (strings{"a", "-fx", "b"}));
    ZEXPECT(parse.args[1].id == test::JoinedId);
}

ZEST_CASE(greedy_unknown_stops_at_dash_dash_and_empty_elements) {
    const auto dash_dash = parse_table(kinds_table(), "--nope a -- b", {.greedy_unknown = true});
    ZASSERT(dash_dash.args.size() == 3U);
    ZEXPECT(dash_dash.values(0) == strings{"a"});
    ZEXPECT(dash_dash.args[1].spelling == "--");
    ZEXPECT(dash_dash.args[1].values.empty());
    ZEXPECT(dash_dash.args[2].id == test::InputId);

    const auto empty =
        parse_table(kinds_table(), test::args("--nope", "a", "", "b"), {.greedy_unknown = true});
    ZASSERT(empty.args.size() == 2U);
    ZEXPECT(empty.values(0) == strings{"a"});
    ZEXPECT(empty.args[1].id == test::InputId);
}

ZEST_CASE(alias_parses_as_its_target) {
    const auto parse = parse_table(kinds_table(), "-h");
    ZASSERT(parse.args.size() == 1U);
    ZEXPECT(parse.args[0].id == test::HelpId);
    ZEXPECT(parse.args[0].spelling == "-h");
    ZEXPECT(parse.args[0].values.empty());
}

ZEST_CASE(alias_args_become_values) {
    const auto parse = parse_table(kinds_table(), "--emit-llvm --trap-defaults");
    ZASSERT(parse.args.size() == 2U);
    ZEXPECT(parse.args[0].id == test::EmitId);
    ZEXPECT(parse.values(0) == strings{"llvm"});
    ZEXPECT(parse.args[1].id == test::TrapId);
    ZEXPECT(parse.values(1) == (strings{"all", "undefined"}));
}

ZEST_CASE(flag_alias_of_a_joined_option_gives_an_empty_value) {
    const auto parse = parse_table(kinds_table(), "--emit-default");
    ZASSERT(parse.args.size() == 1U);
    ZEXPECT(parse.args[0].id == test::EmitId);
    ZEXPECT(parse.values(0) == strings{""});
}

ZEST_CASE(dash_dash_is_unknown_unless_parsed) {
    const auto parse = parse_table(kinds_table(), "-- -f");
    ZASSERT(parse.args.size() == 2U);
    ZEXPECT(parse.args[0].id == test::UnknownId);
    ZEXPECT(parse.args[1].id == test::FlagId);
}

ZEST_CASE(dash_dash_parsing_makes_the_rest_inputs) {
    const auto parse = parse_table(kinds_table(), "-f -- -f --", {.dash_dash_parsing = true});
    ZASSERT(parse.args.size() == 3U);
    ZEXPECT(parse.args[0].id == test::FlagId);
    ZEXPECT(parse.args[1].id == test::InputId);
    ZEXPECT(parse.args[1].spelling == "-f");
    ZEXPECT(parse.args[1].index == 2U);
    ZEXPECT(parse.args[2].id == test::InputId);
    ZEXPECT(parse.args[2].spelling == "--");
}

ZEST_CASE(dash_dash_packing_gathers_the_rest) {
    const auto parse = parse_table(kinds_table(),
                                   "-f -- -f x",
                                   {.dash_dash_parsing = true, .dash_dash_packing = true});
    ZASSERT(parse.args.size() == 2U);
    ZEXPECT(parse.args[1].id == test::InputId);
    ZEXPECT(parse.args[1].spelling == "--");
    ZEXPECT(parse.values(1) == (strings{"-f", "x"}));
    ZEXPECT(parse.args[1].index == 1U);
    ZEXPECT(parse.args[1].next_index == 4U);
}

ZEST_CASE(dash_dash_packing_of_nothing_is_empty) {
    const auto parse =
        parse_table(kinds_table(), "--", {.dash_dash_parsing = true, .dash_dash_packing = true});
    ZASSERT(parse.args.size() == 1U);
    ZEXPECT(parse.args[0].spelling == "--");
    ZEXPECT(parse.args[0].values.empty());
}

ZEST_CASE(next_index_follows_each_argument) {
    const auto parse = parse_table(kinds_table(), "-f -s v --pair a b in");
    ZASSERT(parse.args.size() == 4U);
    ZEXPECT(parse.args[0].next_index == 1U);
    ZEXPECT(parse.args[1].next_index == 3U);
    ZEXPECT(parse.args[2].next_index == 6U);
    ZEXPECT(parse.args[3].next_index == 7U);
}

};  // ZEST_SUITE(deco_option_table_parse)

}  // namespace

}  // namespace kota::option
