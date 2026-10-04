#include <array>
#include <cstdint>
#include <iterator>
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

constexpr std::uint32_t internal_visibility = 1U << 1;
constexpr std::uint32_t experimental_flag = 1U << 6;

// Each id is its option's 1-based index in `filter_infos`.
enum FilterId : std::uint32_t {
    FilterInputId = 1,
    FilterUnknownId,
    PublicId,
    HiddenId,
    ExperimentalId,
};

constexpr auto filter_infos = std::array{
    Option::input(FilterInputId),
    Option::unknown(FilterUnknownId),
    Option::unaliased_one(pfx_double, "--public", PublicId, Kind::Flag, 0),
    Option::unaliased_one(pfx_double,
                          "--hidden",
                          HiddenId,
                          Kind::Flag,
                          0,
                          "",
                          "",
                          0,
                          0,
                          internal_visibility),
    Option::unaliased_one(pfx_double,
                          "--experimental",
                          ExperimentalId,
                          Kind::Flag,
                          0,
                          "",
                          "",
                          0,
                          experimental_flag),
};

// Sorted the way TableGen sorts: by name, ignoring case, a name before any it starts.
enum SortedId : std::uint32_t {
    SortedInputId = 1,
    SortedUnknownId,
    AId,
    BxId,
    BId,
    CId,
};

constexpr auto sorted_infos = std::array{
    Option::input(SortedInputId),
    Option::unknown(SortedUnknownId),
    Option::unaliased_one(pfx_dash, "-a", AId, Kind::Flag, 0),
    Option::unaliased_one(pfx_dash, "-bx", BxId, Kind::Flag, 0),
    Option::unaliased_one(pfx_dash, "-b", BId, Kind::Joined, 1),
    Option::unaliased_one(pfx_dash, "-c", CId, Kind::Separate, 1),
};

ZEST_SUITE(deco_option_table) {

ZEST_CASE(construction_finds_input_and_unknown) {
    const auto& table = kinds_table();
    ZEXPECT(table.input_option_id == test::InputId);
    ZEXPECT(table.unknown_option_id == test::UnknownId);
    ZEXPECT(table.first_searchable_index == 2U);
}

ZEST_CASE(prefixes_are_those_of_every_option) {
    const auto& table = kinds_table();
    ZEXPECT(table.prefixes_union == (std::vector<std::string_view>{"-", "--", "/"}));
    ZEXPECT(table.prefix_chars == (std::vector<char>{'-', '/'}));
}

ZEST_CASE(given_prefixes_replace_the_options_own) {
    // Nothing starts with "+", so every element reads as input.
    const OptTable table(filter_infos, false, {"+"});
    ZEXPECT(table.prefixes_union == std::vector<std::string_view>{"+"});
    ZEXPECT(table.prefix_chars == std::vector<char>{'+'});
    const auto parse = parse_table(table, "--public");
    ZASSERT(parse.args.size() == 1U);
    ZEXPECT(parse.args[0].id == FilterInputId);
}

ZEST_CASE(option_is_found_by_id) {
    const auto& table = kinds_table();
    ZEXPECT(!table.option(0).has_value());
    ZASSERT(table.option(test::FlagId).has_value());
    ZEXPECT(table.option(test::FlagId)->prefixed_name() == "-f");
}

ZEST_CASE(find_option_finds_a_spelling) {
    const auto& table = kinds_table();
    const auto flag = table.find_option("-f");
    ZASSERT(flag.has_value());
    ZEXPECT(flag->id() == test::FlagId);

    // A joined option is found by its name alone.
    const auto emit = table.find_option("--emit=");
    ZASSERT(emit.has_value());
    ZEXPECT(emit->id() == test::EmitId);

    const auto alias = table.find_option("-h");
    ZASSERT(alias.has_value());
    ZEXPECT(alias->id() == test::HelpId);

    ZEXPECT(!table.find_option("--nope").has_value());
}

ZEST_CASE(find_option_takes_the_visibility) {
    const OptTable table(filter_infos);
    ZEXPECT(table.find_option("--hidden").has_value());
    ZEXPECT(!table.find_option("--hidden", DefaultVis).has_value());
    ZEXPECT(table.find_option("--hidden", internal_visibility).has_value());
}

ZEST_CASE(ignore_case_matches_any_case) {
    const auto strict = parse_table(OptTable(filter_infos), "--PUBLIC");
    ZASSERT(strict.args.size() == 1U);
    ZEXPECT(strict.args[0].id == FilterUnknownId);

    const auto loose = parse_table(OptTable(filter_infos, true), "--PUBLIC --Public");
    ZASSERT(loose.args.size() == 2U);
    ZEXPECT(loose.args[0].id == PublicId);
    ZEXPECT(loose.args[1].id == PublicId);
}

ZEST_CASE(tablegen_mode_takes_the_first_match_in_order) {
    OptTable table(sorted_infos);
    table.tablegen_mode = true;
    const auto parse = parse_table(table, "-a -bx -by -b -c v -z");
    ZASSERT(parse.args.size() == 6U);
    ZEXPECT(parse.args[0].id == AId);
    ZEXPECT(parse.args[1].id == BxId);
    ZEXPECT(parse.args[2].id == BId);
    ZEXPECT(parse.values(2) == std::vector<std::string>{"y"});
    ZEXPECT(parse.args[3].id == BId);
    ZEXPECT(parse.values(3) == std::vector<std::string>{""});
    ZEXPECT(parse.args[4].id == CId);
    ZEXPECT(parse.args[5].id == SortedUnknownId);
}

ZEST_CASE(tablegen_mode_missing_value_fails) {
    OptTable table(sorted_infos);
    table.tablegen_mode = true;
    const auto parse = parse_table(table, "-a -c");
    ZASSERT(parse.args.size() == 1U);
    ZASSERT(parse.errors.size() == 1U);
    ZEXPECT(parse.errors[0].error.index == 1U);
    ZEXPECT(std::string_view(parse.errors[0].error.message) == "missing argument value");
}

ZEST_CASE(every_option_is_visible_by_default) {
    const auto parse = parse_table(OptTable(filter_infos), "--public --hidden --experimental");
    ZASSERT(parse.args.size() == 3U);
    ZEXPECT(parse.args[1].id == HiddenId);
}

ZEST_CASE(visibility_hides_other_options) {
    const OptTable table(filter_infos);
    const auto public_only = parse_table(table, "--public --hidden", {.visibility = DefaultVis});
    ZASSERT(public_only.args.size() == 2U);
    ZEXPECT(public_only.args[0].id == PublicId);
    ZEXPECT(public_only.args[1].id == FilterUnknownId);

    const auto internal = parse_table(table, "--hidden", {.visibility = internal_visibility});
    ZASSERT(internal.args.size() == 1U);
    ZEXPECT(internal.args[0].id == HiddenId);
}

ZEST_CASE(include_flags_keep_only_flagged_options) {
    const auto parse = parse_table(OptTable(filter_infos),
                                   "--experimental --public",
                                   {.include_flags = experimental_flag});
    ZASSERT(parse.args.size() == 2U);
    ZEXPECT(parse.args[0].id == ExperimentalId);
    ZEXPECT(parse.args[1].id == FilterUnknownId);
}

ZEST_CASE(exclude_flags_drop_flagged_options) {
    const auto parse = parse_table(OptTable(filter_infos),
                                   "--experimental --public",
                                   {.exclude_flags = experimental_flag});
    ZASSERT(parse.args.size() == 2U);
    ZEXPECT(parse.args[0].id == FilterUnknownId);
    ZEXPECT(parse.args[1].id == PublicId);
}

ZEST_CASE(greedy_unknown_takes_an_option_filtered_out) {
    const auto parse = parse_table(OptTable(filter_infos),
                                   "--nope --hidden --public",
                                   {.greedy_unknown = true, .visibility = DefaultVis});
    ZASSERT(parse.args.size() == 2U);
    ZEXPECT(parse.values(0) == std::vector<std::string>{"--hidden"});
    ZEXPECT(parse.args[1].id == PublicId);
}

ZEST_CASE(parse_iterates_one_argument_at_a_time) {
    auto argv = test::split("-f in");
    auto parse = kinds_table().parse(argv);
    auto it = parse.begin();
    ZASSERT(it != parse.end());
    const auto first = it++;
    ZASSERT(first->has_value());
    ZEXPECT((*first)->id == test::FlagId);
    ZASSERT(it != parse.end());
    ZASSERT(it->has_value());
    ZEXPECT((*it)->id == test::InputId);
    ++it;
    ZEXPECT(it == parse.end());
    ZEXPECT(std::ranges::distance(kinds_table().parse(argv)) == 2);
}

ZEST_CASE(parse_of_nothing_is_empty) {
    std::vector<std::string> argv;
    auto parse = kinds_table().parse(argv);
    ZEXPECT(parse.begin() == parse.end());
}

};  // ZEST_SUITE(deco_option_table)

}  // namespace

}  // namespace kota::option
