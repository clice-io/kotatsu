#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "deco/harness/option_table.h"
#include "kota/deco/option.h"
#include "kota/zest/zest.h"

namespace kota::option {

namespace {

using test::parse_table;
using strings = std::vector<std::string>;

// Each id is its option's 1-based index in `infos`.
enum OptId : std::uint32_t {
    InputId = 1,
    UnknownId,
    AId,
    BId,
    SeparateId,
    JoinedId,
    PairId,
    OutputId,
    LongId,
};

constexpr auto infos = std::array{
    Option::input(InputId),
    Option::unknown(UnknownId),
    Option::unaliased_one(pfx_dash, "-a", AId, Kind::Flag, 0),
    Option::unaliased_one(pfx_dash, "-b", BId, Kind::Flag, 0),
    Option::unaliased_one(pfx_dash, "-s", SeparateId, Kind::Separate, 1),
    Option::unaliased_one(pfx_dash, "-j", JoinedId, Kind::Joined, 1),
    Option::unaliased_one(pfx_dash, "-p", PairId, Kind::MultiArg, 2),
    Option::unaliased_one(pfx_dash, "-o", OutputId, Kind::JoinedOrSeparate, 1),
    Option::unaliased_one(pfx_double, "--long", LongId, Kind::Flag, 0),
};

/// Parses `line` with grouped short options on.
test::TableParse parse_grouped(std::string_view line) {
    return parse_table(OptTable(infos), line, {.grouped_short_options = true});
}

ZEST_SUITE(deco_option_table_grouped) {

ZEST_CASE(group_of_flags_parses_as_each) {
    const auto parse = parse_grouped("-ab c");
    EXPECT(parse.errors.empty());
    ASSERT(parse.args.size() == 3U);
    EXPECT(parse.args[0].id == AId);
    EXPECT(parse.args[0].spelling == "-a");
    EXPECT(parse.args[1].id == BId);
    EXPECT(parse.args[1].spelling == "-b");
    EXPECT(parse.args[2].id == InputId);
}

ZEST_CASE(group_stays_on_its_element_until_used_up) {
    const auto parse = parse_grouped("-ab c");
    ASSERT(parse.args.size() == 3U);
    EXPECT(parse.args[0].index == 0U);
    EXPECT(parse.args[0].next_index == 0U);
    EXPECT(parse.args[1].index == 0U);
    EXPECT(parse.args[1].next_index == 1U);
}

ZEST_CASE(whole_option_is_no_group) {
    const auto parse = parse_grouped("-a -jfoo --long");
    ASSERT(parse.args.size() == 3U);
    EXPECT(parse.args[0].id == AId);
    EXPECT(parse.args[1].id == JoinedId);
    EXPECT(parse.values(1) == strings{"foo"});
    EXPECT(parse.args[2].id == LongId);
}

ZEST_CASE(group_ends_in_a_joined_value) {
    const auto parse = parse_grouped("-ajfoo");
    ASSERT(parse.args.size() == 2U);
    EXPECT(parse.args[1].id == JoinedId);
    EXPECT(parse.values(1) == strings{"foo"});
}

ZEST_CASE(joined_value_in_a_group_views_argv) {
    // The value is the tail of the element, not of a copy the parser keeps.
    const auto parse = parse_grouped("-ajfoo");
    ASSERT(parse.args.size() == 2U);
    ASSERT(parse.args[1].values.size() == 1U);
    EXPECT(parse.args[1].values[0].data() == parse.argv[0].data() + 3);
}

ZEST_CASE(joined_values_of_groups_stay_apart) {
    const auto parse = parse_grouped("-ajfoo -bjbar");
    ASSERT(parse.args.size() == 4U);
    EXPECT(parse.values(1) == strings{"foo"});
    EXPECT(parse.values(3) == strings{"bar"});
}

ZEST_CASE(group_ending_in_a_separate_option_takes_the_next_element) {
    const auto parse = parse_grouped("-as out -b");
    EXPECT(parse.errors.empty());
    ASSERT(parse.args.size() == 3U);
    EXPECT(parse.args[1].id == SeparateId);
    EXPECT(parse.args[1].spelling == "-s");
    EXPECT(parse.values(1) == strings{"out"});
    EXPECT(parse.args[1].index == 0U);
    EXPECT(parse.args[1].next_index == 2U);
    EXPECT(parse.args[2].id == BId);
}

ZEST_CASE(group_ending_in_a_multi_arg_option_takes_its_values) {
    const auto parse = parse_grouped("-bp x y");
    ASSERT(parse.args.size() == 2U);
    EXPECT(parse.args[1].id == PairId);
    EXPECT(parse.values(1) == (strings{"x", "y"}));
}

ZEST_CASE(group_ending_in_joined_or_separate_takes_either) {
    const auto joined = parse_grouped("-aoV");
    ASSERT(joined.args.size() == 2U);
    EXPECT(joined.args[1].id == OutputId);
    EXPECT(joined.values(1) == strings{"V"});

    const auto separate = parse_grouped("-ao V");
    ASSERT(separate.args.size() == 2U);
    EXPECT(separate.args[1].id == OutputId);
    EXPECT(separate.values(1) == strings{"V"});
}

ZEST_CASE(group_missing_a_value_fails) {
    const auto parse = parse_grouped("-as");
    ASSERT(parse.args.size() == 1U);
    EXPECT(parse.args[0].id == AId);
    ASSERT(parse.errors.size() == 1U);
    EXPECT(parse.errors[0].after == 1U);
    EXPECT(parse.errors[0].error.index == 0U);
    EXPECT(std::string_view(parse.errors[0].error.message) == "missing argument value");
}

ZEST_CASE(unknown_letters_split_off_one_by_one) {
    const auto parse = parse_grouped("-zxa");
    ASSERT(parse.args.size() == 3U);
    EXPECT(parse.args[0].id == UnknownId);
    EXPECT(parse.args[0].spelling == "-z");
    EXPECT(parse.args[1].id == UnknownId);
    EXPECT(parse.args[1].spelling == "-x");
    EXPECT(parse.args[2].id == AId);
    EXPECT(parse.args[2].next_index == 1U);
}

ZEST_CASE(flag_given_a_value_is_unknown) {
    const auto parse = parse_grouped("-a=1");
    ASSERT(parse.args.size() == 1U);
    EXPECT(parse.args[0].id == UnknownId);
    EXPECT(parse.args[0].spelling == "-a=1");
}

ZEST_CASE(unknown_long_option_is_no_group) {
    const auto parse = parse_grouped("--longer --");
    ASSERT(parse.args.size() == 2U);
    EXPECT(parse.args[0].id == UnknownId);
    EXPECT(parse.args[0].spelling == "--longer");
    EXPECT(parse.args[1].id == UnknownId);
    EXPECT(parse.args[1].spelling == "--");
}

ZEST_CASE(inputs_are_no_groups) {
    const auto parse = parse_grouped("file -");
    ASSERT(parse.args.size() == 2U);
    EXPECT(parse.args[0].id == InputId);
    EXPECT(parse.args[1].id == InputId);
}

};  // ZEST_SUITE(deco_option_table_grouped)

}  // namespace

}  // namespace kota::option
