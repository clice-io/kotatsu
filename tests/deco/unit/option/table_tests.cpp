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
    EXPECT(table.input_option_id == test::InputId);
    EXPECT(table.unknown_option_id == test::UnknownId);
    EXPECT(table.first_searchable_index == 2U);
}

ZEST_CASE(prefixes_are_those_of_every_option) {
    const auto& table = kinds_table();
    EXPECT(table.prefixes_union == (std::vector<std::string_view>{"-", "--", "/"}));
    EXPECT(table.prefix_chars == (std::vector<char>{'-', '/'}));
}

ZEST_CASE(given_prefixes_replace_the_options_own) {
    // Nothing starts with "+", so every element reads as input.
    const OptTable table(filter_infos, false, {"+"});
    EXPECT(table.prefixes_union == std::vector<std::string_view>{"+"});
    EXPECT(table.prefix_chars == std::vector<char>{'+'});
    const auto parse = parse_table(table, "--public");
    ASSERT(parse.args.size() == 1U);
    EXPECT(parse.args[0].id == FilterInputId);
}

ZEST_CASE(option_is_found_by_id) {
    const auto& table = kinds_table();
    EXPECT(!table.option(0).has_value());
    ASSERT(table.option(test::FlagId).has_value());
    EXPECT(table.option(test::FlagId)->prefixed_name() == "-f");
}

ZEST_CASE(find_option_finds_a_spelling) {
    const auto& table = kinds_table();
    const auto flag = table.find_option("-f");
    ASSERT(flag.has_value());
    EXPECT(flag->id() == test::FlagId);

    // A joined option is found by its name alone.
    const auto emit = table.find_option("--emit=");
    ASSERT(emit.has_value());
    EXPECT(emit->id() == test::EmitId);

    const auto alias = table.find_option("-h");
    ASSERT(alias.has_value());
    EXPECT(alias->id() == test::HelpId);

    EXPECT(!table.find_option("--nope").has_value());
}

ZEST_CASE(find_option_takes_the_visibility) {
    const OptTable table(filter_infos);
    EXPECT(table.find_option("--hidden").has_value());
    EXPECT(!table.find_option("--hidden", DefaultVis).has_value());
    EXPECT(table.find_option("--hidden", internal_visibility).has_value());
}

ZEST_CASE(ignore_case_matches_any_case) {
    const auto strict = parse_table(OptTable(filter_infos), "--PUBLIC");
    ASSERT(strict.args.size() == 1U);
    EXPECT(strict.args[0].id == FilterUnknownId);

    const auto loose = parse_table(OptTable(filter_infos, true), "--PUBLIC --Public");
    ASSERT(loose.args.size() == 2U);
    EXPECT(loose.args[0].id == PublicId);
    EXPECT(loose.args[1].id == PublicId);
}

ZEST_CASE(tablegen_mode_takes_the_first_match_in_order) {
    OptTable table(sorted_infos);
    table.tablegen_mode = true;
    const auto parse = parse_table(table, "-a -bx -by -b -c v -z");
    ASSERT(parse.args.size() == 6U);
    EXPECT(parse.args[0].id == AId);
    EXPECT(parse.args[1].id == BxId);
    EXPECT(parse.args[2].id == BId);
    EXPECT(parse.values(2) == std::vector<std::string>{"y"});
    EXPECT(parse.args[3].id == BId);
    EXPECT(parse.values(3) == std::vector<std::string>{""});
    EXPECT(parse.args[4].id == CId);
    EXPECT(parse.args[5].id == SortedUnknownId);
}

ZEST_CASE(tablegen_mode_missing_value_fails) {
    OptTable table(sorted_infos);
    table.tablegen_mode = true;
    const auto parse = parse_table(table, "-a -c");
    ASSERT(parse.args.size() == 1U);
    ASSERT(parse.errors.size() == 1U);
    EXPECT(parse.errors[0].error.index == 1U);
    EXPECT(std::string_view(parse.errors[0].error.message) == "missing argument value");
}

ZEST_CASE(every_option_is_visible_by_default) {
    const auto parse = parse_table(OptTable(filter_infos), "--public --hidden --experimental");
    ASSERT(parse.args.size() == 3U);
    EXPECT(parse.args[1].id == HiddenId);
}

ZEST_CASE(visibility_hides_other_options) {
    const OptTable table(filter_infos);
    const auto public_only = parse_table(table, "--public --hidden", {.visibility = DefaultVis});
    ASSERT(public_only.args.size() == 2U);
    EXPECT(public_only.args[0].id == PublicId);
    EXPECT(public_only.args[1].id == FilterUnknownId);

    const auto internal = parse_table(table, "--hidden", {.visibility = internal_visibility});
    ASSERT(internal.args.size() == 1U);
    EXPECT(internal.args[0].id == HiddenId);
}

ZEST_CASE(include_flags_keep_only_flagged_options) {
    const auto parse = parse_table(OptTable(filter_infos),
                                   "--experimental --public",
                                   {.include_flags = experimental_flag});
    ASSERT(parse.args.size() == 2U);
    EXPECT(parse.args[0].id == ExperimentalId);
    EXPECT(parse.args[1].id == FilterUnknownId);
}

ZEST_CASE(exclude_flags_drop_flagged_options) {
    const auto parse = parse_table(OptTable(filter_infos),
                                   "--experimental --public",
                                   {.exclude_flags = experimental_flag});
    ASSERT(parse.args.size() == 2U);
    EXPECT(parse.args[0].id == FilterUnknownId);
    EXPECT(parse.args[1].id == PublicId);
}

ZEST_CASE(greedy_unknown_takes_an_option_filtered_out) {
    const auto parse = parse_table(OptTable(filter_infos),
                                   "--nope --hidden --public",
                                   {.greedy_unknown = true, .visibility = DefaultVis});
    ASSERT(parse.args.size() == 2U);
    EXPECT(parse.values(0) == std::vector<std::string>{"--hidden"});
    EXPECT(parse.args[1].id == PublicId);
}

ZEST_CASE(parse_iterates_one_argument_at_a_time) {
    auto argv = test::split("-f in");
    auto parse = kinds_table().parse(argv);
    auto it = parse.begin();
    ASSERT(it != parse.end());
    const auto first = it++;
    ASSERT(first->has_value());
    EXPECT((*first)->id == test::FlagId);
    ASSERT(it != parse.end());
    ASSERT(it->has_value());
    EXPECT((*it)->id == test::InputId);
    ++it;
    EXPECT(it == parse.end());
    EXPECT(std::ranges::distance(kinds_table().parse(argv)) == 2);
}

ZEST_CASE(parse_of_nothing_is_empty) {
    std::vector<std::string> argv;
    auto parse = kinds_table().parse(argv);
    EXPECT(parse.begin() == parse.end());
}

};  // ZEST_SUITE(deco_option_table)

}  // namespace

}  // namespace kota::option
