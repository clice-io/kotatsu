#include <array>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "test_util.h"
#include "kota/deco/option.h"
#include "kota/zest/zest.h"

namespace kota::option {
namespace {

using namespace std::literals::string_view_literals;
using test::parse_all;
using test::ParseCapture;
using test::split2vec;

enum MainOptionID {
    MAIN_OPT_INVALID = 0,
    MAIN_OPT_INPUT = 1,
    MAIN_OPT_UNKNOWN = 2,
    MAIN_OPT_HELP,
    MAIN_OPT_HELP_SHORT,
    MAIN_OPT_SCRIPT,
};

constexpr auto kMainOptInfos = std::array{
    Option::input(MAIN_OPT_INPUT),
    Option::unknown(MAIN_OPT_UNKNOWN),
    Option::unaliased_one(pfx_double, "--help", MAIN_OPT_HELP, Kind::Flag, 0, "Display help", ""),
    Option::unaliased_one(pfx_dash, "-h", MAIN_OPT_HELP_SHORT, Kind::Flag, 0, "Display help", "")
        .alias_of(MAIN_OPT_HELP),
    Option::unaliased_one(pfx_dash, "-s", MAIN_OPT_SCRIPT, Kind::Separate, 1, "Script path", ""),
};

OptTable make_main_opt_table() {
    return OptTable(std::span<const Option>(kMainOptInfos));
}

ParseOptions make_main_parse_options() {
    ParseOptions opts;
    opts.dash_dash_parsing = true;
    opts.dash_dash_packing = true;
    return opts;
}

enum ProxyOptionID {
    PROXY_OPT_INVALID = 0,
    PROXY_OPT_INPUT = 1,
    PROXY_OPT_UNKNOWN = 2,
    PROXY_OPT_PARENT_ID,
    PROXY_OPT_EXEC,
};

constexpr auto kProxyOptInfos = std::array{
    Option::input(PROXY_OPT_INPUT),
    Option::unknown(PROXY_OPT_UNKNOWN),
    Option::unaliased_one(pfx_dash,
                          "-p",
                          PROXY_OPT_PARENT_ID,
                          Kind::Separate,
                          1,
                          "Parent process id",
                          ""),
    Option::unaliased_one(pfx_dash_double, "--exec", PROXY_OPT_EXEC, Kind::Separate, 1, "Exec", ""),
};

OptTable make_proxy_opt_table() {
    return OptTable(std::span<const Option>(kProxyOptInfos));
}

ParseOptions make_proxy_parse_options() {
    ParseOptions opts;
    opts.dash_dash_parsing = true;
    opts.dash_dash_packing = true;
    opts.greedy_unknown = true;
    return opts;
}

ZEST_SUITE(option_parse_view) {

ZEST_CASE(main_option_table_basic) {
    auto table = make_main_opt_table();
    auto opts = make_main_parse_options();
    auto parsed = parse_all(
        table,
        split2vec("-p 1234 -s script::profile --dest=114514 -- /usr/bin/clang++ --version"),
        opts);

    EXPECT(parsed.errors.empty());
    ASSERT(parsed.args.size() == 5U);

    EXPECT(parsed.args[0].id == MAIN_OPT_UNKNOWN);
    EXPECT(parsed.args[0].spelling == "-p");
    EXPECT(parsed.args[0].index == 0U);

    EXPECT(parsed.args[1].id == MAIN_OPT_INPUT);
    EXPECT(parsed.args[1].spelling == "1234");
    EXPECT(parsed.args[1].index == 1U);

    EXPECT(parsed.args[2].id == MAIN_OPT_SCRIPT);
    ASSERT(parsed.args[2].values.size() == 1U);
    EXPECT(parsed.args[2].values[0] == "script::profile");
    EXPECT(parsed.args[2].spelling == "-s");
    EXPECT(parsed.args[2].index == 2U);

    EXPECT(parsed.args[3].id == MAIN_OPT_UNKNOWN);
    EXPECT(parsed.args[3].spelling == "--dest=114514");
    EXPECT(parsed.args[3].index == 4U);

    EXPECT(parsed.args[4].id == MAIN_OPT_INPUT);
    EXPECT(parsed.args[4].spelling == "--");
    ASSERT(parsed.args[4].values.size() == 2U);
    EXPECT(parsed.args[4].values[0] == "/usr/bin/clang++");
    EXPECT(parsed.args[4].values[1] == "--version");
    EXPECT(parsed.args[4].index == 5U);
}

ZEST_CASE(alias_resolves_to_canonical) {
    auto table = make_main_opt_table();
    auto opts = make_main_parse_options();
    auto parsed = parse_all(table, split2vec("-h"), opts);
    EXPECT(parsed.errors.empty());
    ASSERT(parsed.args.size() == 1U);
    EXPECT(parsed.args[0].id == MAIN_OPT_HELP);
    EXPECT(parsed.args[0].spelling == "-h");
    EXPECT(parsed.args[0].values.size() == 0U);
}

ZEST_CASE(proxy_option_table_basic) {
    auto table = make_proxy_opt_table();
    auto opts = make_proxy_parse_options();

    auto parsed = parse_all(table, split2vec("-p 1234"), opts);
    EXPECT(parsed.errors.empty());
    ASSERT(parsed.args.size() == 1U);
    EXPECT(parsed.args[0].id == PROXY_OPT_PARENT_ID);
    ASSERT(parsed.args[0].values.size() == 1U);
    EXPECT(parsed.args[0].values[0] == "1234");
    EXPECT(parsed.args[0].spelling == "-p");
    EXPECT(parsed.args[0].index == 0U);

    parsed = parse_all(table, split2vec("--exec /bin/ls"), opts);
    EXPECT(parsed.errors.empty());
    ASSERT(parsed.args.size() == 1U);
    EXPECT(parsed.args[0].id == PROXY_OPT_EXEC);
    ASSERT(parsed.args[0].values.size() == 1U);
    EXPECT(parsed.args[0].values[0] == "/bin/ls");
    EXPECT(parsed.args[0].spelling == "--exec");
    EXPECT(parsed.args[0].index == 0U);

    parsed =
        parse_all(table, split2vec("-p 12 --exec /usr/bin/clang++ -- clang++ --version"), opts);
    EXPECT(parsed.errors.empty());
    ASSERT(parsed.args.size() == 3U);

    EXPECT(parsed.args[0].id == PROXY_OPT_PARENT_ID);
    ASSERT(parsed.args[0].values.size() == 1U);
    EXPECT(parsed.args[0].values[0] == "12");
    EXPECT(parsed.args[0].index == 0U);

    EXPECT(parsed.args[1].id == PROXY_OPT_EXEC);
    ASSERT(parsed.args[1].values.size() == 1U);
    EXPECT(parsed.args[1].values[0] == "/usr/bin/clang++");
    EXPECT(parsed.args[1].index == 2U);

    EXPECT(parsed.args[2].id == PROXY_OPT_INPUT);
    EXPECT(parsed.args[2].spelling == "--");
    ASSERT(parsed.args[2].values.size() == 2U);
    EXPECT(parsed.args[2].values[0] == "clang++");
    EXPECT(parsed.args[2].values[1] == "--version");
    EXPECT(parsed.args[2].index == 4U);
}

ZEST_CASE(proxy_missing_value_error) {
    auto table = make_proxy_opt_table();
    auto opts = make_proxy_parse_options();
    auto parsed = parse_all(table, split2vec("-p"), opts);
    EXPECT(parsed.args.size() == 0U);
    ASSERT(parsed.errors.size() == 1U);
    EXPECT(std::string_view(parsed.errors[0].message).contains("missing"));
}

ZEST_CASE(unknown_consumes_until_known) {
    auto table = make_proxy_opt_table();
    auto opts = make_proxy_parse_options();
    auto parsed = parse_all(table, split2vec("--unknown-cmd xxx yyy -p 1234"), opts);

    EXPECT(parsed.errors.empty());
    ASSERT(parsed.args.size() == 2U);

    EXPECT(parsed.args[0].id == PROXY_OPT_UNKNOWN);
    EXPECT(parsed.args[0].spelling == "--unknown-cmd");
    ASSERT(parsed.args[0].values.size() == 2U);
    EXPECT(parsed.args[0].values[0] == "xxx");
    EXPECT(parsed.args[0].values[1] == "yyy");

    EXPECT(parsed.args[1].id == PROXY_OPT_PARENT_ID);
    ASSERT(parsed.args[1].values.size() == 1U);
    EXPECT(parsed.args[1].values[0] == "1234");
}

ZEST_CASE(unknown_consumes_all_when_no_known_follows) {
    auto table = make_proxy_opt_table();
    auto opts = make_proxy_parse_options();
    auto parsed = parse_all(table, split2vec("--unknown xxx yyy"), opts);

    EXPECT(parsed.errors.empty());
    ASSERT(parsed.args.size() == 1U);

    EXPECT(parsed.args[0].id == PROXY_OPT_UNKNOWN);
    EXPECT(parsed.args[0].spelling == "--unknown");
    ASSERT(parsed.args[0].values.size() == 2U);
    EXPECT(parsed.args[0].values[0] == "xxx");
    EXPECT(parsed.args[0].values[1] == "yyy");
}

ZEST_CASE(consecutive_unknown_prefixed) {
    auto table = make_proxy_opt_table();
    auto opts = make_proxy_parse_options();
    auto parsed = parse_all(table, split2vec("--unknown1 --unknown2 -p 1234"), opts);

    EXPECT(parsed.errors.empty());
    ASSERT(parsed.args.size() == 2U);

    EXPECT(parsed.args[0].id == PROXY_OPT_UNKNOWN);
    EXPECT(parsed.args[0].spelling == "--unknown1");
    ASSERT(parsed.args[0].values.size() == 1U);
    EXPECT(parsed.args[0].values[0] == "--unknown2");

    EXPECT(parsed.args[1].id == PROXY_OPT_PARENT_ID);
    ASSERT(parsed.args[1].values.size() == 1U);
    EXPECT(parsed.args[1].values[0] == "1234");
}

ZEST_CASE(unknown_prefix_match_not_treated_as_boundary) {
    auto table = make_proxy_opt_table();
    auto opts = make_proxy_parse_options();
    auto parsed = parse_all(table, split2vec("--unknown --execute-thing -p 1234"), opts);

    EXPECT(parsed.errors.empty());
    ASSERT(parsed.args.size() == 2U);

    EXPECT(parsed.args[0].id == PROXY_OPT_UNKNOWN);
    EXPECT(parsed.args[0].spelling == "--unknown");
    ASSERT(parsed.args[0].values.size() == 1U);
    EXPECT(parsed.args[0].values[0] == "--execute-thing");

    EXPECT(parsed.args[1].id == PROXY_OPT_PARENT_ID);
    ASSERT(parsed.args[1].values.size() == 1U);
    EXPECT(parsed.args[1].values[0] == "1234");
}

};  // ZEST_SUITE(option_parse_view)

enum GroupedOptionID {
    GROUPED_OPT_INVALID = 0,
    GROUPED_OPT_INPUT = 1,
    GROUPED_OPT_UNKNOWN = 2,
    GROUPED_OPT_A,
    GROUPED_OPT_B,
};

constexpr auto kGroupedOptInfos = std::array{
    Option::input(GROUPED_OPT_INPUT),
    Option::unknown(GROUPED_OPT_UNKNOWN),
    Option::unaliased_one(pfx_dash, "-a", GROUPED_OPT_A, Kind::Flag, 0, "", ""),
    Option::unaliased_one(pfx_dash, "-b", GROUPED_OPT_B, Kind::Flag, 0, "", ""),
};

OptTable make_grouped_opt_table() {
    return OptTable(std::span<const Option>(kGroupedOptInfos));
}

ParseOptions make_grouped_parse_options() {
    ParseOptions opts;
    opts.grouped_short_options = true;
    return opts;
}

enum IgnoreCaseOptionID {
    IGNORE_CASE_OPT_INVALID = 0,
    IGNORE_CASE_OPT_INPUT = 1,
    IGNORE_CASE_OPT_UNKNOWN = 2,
    IGNORE_CASE_OPT_HELP,
};

constexpr auto kIgnoreCaseOptInfos = std::array{
    Option::input(IGNORE_CASE_OPT_INPUT),
    Option::unknown(IGNORE_CASE_OPT_UNKNOWN),
    Option::unaliased_one(pfx_double, "--help", IGNORE_CASE_OPT_HELP, Kind::Flag, 0, "", ""),
};

OptTable make_ignore_case_opt_table(bool ignore_case) {
    return OptTable(std::span<const Option>(kIgnoreCaseOptInfos), ignore_case);
}

enum FilterOptionID {
    FILTER_OPT_INVALID = 0,
    FILTER_OPT_INPUT = 1,
    FILTER_OPT_UNKNOWN = 2,
    FILTER_OPT_PUBLIC,
    FILTER_OPT_HIDDEN,
    FILTER_OPT_FLAGGED,
};

constexpr std::uint32_t kInternalVisibility = 1U << 1;
constexpr std::uint32_t kExperimentalFlag = 1U << 6;

constexpr auto kFilterOptInfos = std::array{
    Option::input(FILTER_OPT_INPUT),
    Option::unknown(FILTER_OPT_UNKNOWN),
    Option::unaliased_one(pfx_double, "--public", FILTER_OPT_PUBLIC, Kind::Flag, 0, "", ""),
    Option::unaliased_one(pfx_double,
                          "--hidden",
                          FILTER_OPT_HIDDEN,
                          Kind::Flag,
                          0,
                          "",
                          "",
                          0,
                          0,
                          kInternalVisibility),
    Option::unaliased_one(pfx_double,
                          "--flagged",
                          FILTER_OPT_FLAGGED,
                          Kind::Flag,
                          0,
                          "",
                          "",
                          0,
                          kExperimentalFlag,
                          DefaultVis),
};

OptTable make_filter_opt_table() {
    return OptTable(std::span<const Option>(kFilterOptInfos));
}

enum KindsOptionID {
    KINDS_OPT_INVALID = 0,
    KINDS_OPT_INPUT = 1,
    KINDS_OPT_UNKNOWN = 2,
    KINDS_OPT_JOINED,
    KINDS_OPT_COMMA_JOINED,
    KINDS_OPT_MULTI_ARG,
    KINDS_OPT_JOINED_OR_SEPARATE,
    KINDS_OPT_JOINED_AND_SEPARATE,
    KINDS_OPT_REMAINING,
    KINDS_OPT_REMAINING_JOINED,
};

constexpr auto kKindsOptInfos = std::array{
    Option::input(KINDS_OPT_INPUT),
    Option::unknown(KINDS_OPT_UNKNOWN),
    Option::unaliased_one(pfx_dash, "-j", KINDS_OPT_JOINED, Kind::Joined, 1, "", ""),
    Option::unaliased_one(pfx_double,
                          "--list",
                          KINDS_OPT_COMMA_JOINED,
                          Kind::CommaJoined,
                          1,
                          "",
                          ""),
    Option::unaliased_one(pfx_double, "--pair", KINDS_OPT_MULTI_ARG, Kind::MultiArg, 2, "", ""),
    Option::unaliased_one(pfx_dash,
                          "-o",
                          KINDS_OPT_JOINED_OR_SEPARATE,
                          Kind::JoinedOrSeparate,
                          1,
                          "",
                          ""),
    Option::unaliased_one(pfx_dash,
                          "-x",
                          KINDS_OPT_JOINED_AND_SEPARATE,
                          Kind::JoinedAndSeparate,
                          2,
                          "",
                          ""),
    Option::unaliased_one(pfx_double,
                          "--rest",
                          KINDS_OPT_REMAINING,
                          Kind::RemainingArgs,
                          0,
                          "",
                          ""),
    Option::unaliased_one(pfx_double,
                          "--tail",
                          KINDS_OPT_REMAINING_JOINED,
                          Kind::RemainingArgsJoined,
                          0,
                          "",
                          ""),
};

OptTable make_kinds_opt_table() {
    return OptTable(std::span<const Option>(kKindsOptInfos));
}

enum MatchOptionID {
    MATCH_OPT_INVALID = 0,
    MATCH_OPT_INPUT = 1,
    MATCH_OPT_UNKNOWN = 2,
    MATCH_OPT_GROUP,
    MATCH_OPT_MEMBER,
    MATCH_OPT_ALIAS_MEMBER,
    MATCH_OPT_JOINED,
    MATCH_OPT_OVERRIDE_FLAG,
};

constexpr auto kMatchOptInfos = std::array{
    Option::input(MATCH_OPT_INPUT),
    Option::unknown(MATCH_OPT_UNKNOWN),
    Option::unaliased_one(pfx_none, "group", MATCH_OPT_GROUP, Kind::Group, 0, "", ""),
    Option::unaliased_one(pfx_dash, "-m", MATCH_OPT_MEMBER, Kind::Flag, 0, "", "", MATCH_OPT_GROUP),
    Option::unaliased_one(pfx_dash, "-am", MATCH_OPT_ALIAS_MEMBER, Kind::Flag, 0, "", "")
        .alias_of(MATCH_OPT_MEMBER),
    Option::unaliased_one(pfx_dash, "-j", MATCH_OPT_JOINED, Kind::Joined, 1, "", ""),
    Option::unaliased_one(pfx_dash,
                          "-r",
                          MATCH_OPT_OVERRIDE_FLAG,
                          Kind::Flag,
                          0,
                          "",
                          "",
                          0,
                          RenderJoined),
};

OptTable make_match_opt_table() {
    return OptTable(std::span<const Option>(kMatchOptInfos));
}

enum AliasOptionID {
    ALIAS_OPT_INVALID = 0,
    ALIAS_OPT_INPUT = 1,
    ALIAS_OPT_UNKNOWN = 2,
    ALIAS_OPT_TRAP_EQ,
    ALIAS_OPT_TRAP_DEFAULTS,
    ALIAS_OPT_EMIT_EQ,
    ALIAS_OPT_EMIT_LLVM,
};

constexpr auto kAliasOptInfos = std::array{
    Option::input(ALIAS_OPT_INPUT),
    Option::unknown(ALIAS_OPT_UNKNOWN),
    Option::unaliased_one(pfx_double, "--trap=", ALIAS_OPT_TRAP_EQ, Kind::CommaJoined, 1, "", ""),
    Option::unaliased_one(pfx_double,
                          "--trap-defaults",
                          ALIAS_OPT_TRAP_DEFAULTS,
                          Kind::Flag,
                          0,
                          "",
                          "")
        .alias_of(ALIAS_OPT_TRAP_EQ, "all\0undefined\0"),
    Option::unaliased_one(pfx_double, "--emit=", ALIAS_OPT_EMIT_EQ, Kind::Joined, 1, "", ""),
    Option::unaliased_one(pfx_double, "--emit-llvm", ALIAS_OPT_EMIT_LLVM, Kind::Flag, 0, "", "")
        .alias_of(ALIAS_OPT_EMIT_EQ),
};

OptTable make_alias_opt_table() {
    return OptTable(std::span<const Option>(kAliasOptInfos));
}

ZEST_SUITE(option_extended_coverage) {

ZEST_CASE(ignore_case_controls_matching) {
    auto strict_table = make_ignore_case_opt_table(false);
    auto parsed = parse_all(strict_table, split2vec("--HELP"));
    EXPECT(parsed.errors.empty());
    ASSERT(parsed.args.size() == 1U);
    EXPECT(parsed.args[0].id == IGNORE_CASE_OPT_UNKNOWN);

    auto ignore_case_table = make_ignore_case_opt_table(true);
    parsed = parse_all(ignore_case_table, split2vec("--HELP"));
    EXPECT(parsed.errors.empty());
    ASSERT(parsed.args.size() == 1U);
    EXPECT(parsed.args[0].id == IGNORE_CASE_OPT_HELP);
}

ZEST_CASE(grouped_short_option_parsing) {
    auto table = make_grouped_opt_table();
    auto opts = make_grouped_parse_options();

    auto parsed = parse_all(table, split2vec("-ab"), opts);
    EXPECT(parsed.errors.empty());
    ASSERT(parsed.args.size() == 2U);
    EXPECT(parsed.args[0].id == GROUPED_OPT_A);
    EXPECT(parsed.args[1].id == GROUPED_OPT_B);
}

ZEST_CASE(grouped_unknown_splits) {
    auto table = make_grouped_opt_table();
    auto opts = make_grouped_parse_options();

    auto parsed = parse_all(table, split2vec("-zx"), opts);
    EXPECT(parsed.errors.empty());
    ASSERT(parsed.args.size() == 2U);
    EXPECT(parsed.args[0].id == GROUPED_OPT_UNKNOWN);
    EXPECT(parsed.args[1].id == GROUPED_OPT_UNKNOWN);
}

ZEST_CASE(grouped_equals_form_is_unknown) {
    auto table = make_grouped_opt_table();
    auto opts = make_grouped_parse_options();

    auto parsed = parse_all(table, split2vec("-a=1"), opts);
    EXPECT(parsed.errors.empty());
    ASSERT(parsed.args.size() == 1U);
    EXPECT(parsed.args[0].id == GROUPED_OPT_UNKNOWN);
}

ZEST_CASE(visibility_filter_affects_matching) {
    auto table = make_filter_opt_table();

    auto argv = split2vec("--hidden");
    ParseCapture default_vis;
    ParseOptions default_opts;
    default_opts.visibility = DefaultVis;
    for(auto& result: table.parse(argv, default_opts)) {
        if(result.has_value())
            default_vis.args.push_back(*result);
        else
            default_vis.errors.push_back(result.error());
    }
    EXPECT(default_vis.errors.empty());
    ASSERT(default_vis.args.size() == 1U);
    EXPECT(default_vis.args[0].id == FILTER_OPT_UNKNOWN);

    argv = split2vec("--hidden");
    ParseCapture capture;
    ParseOptions internal_opts;
    internal_opts.visibility = kInternalVisibility;
    for(auto& result: table.parse(argv, internal_opts)) {
        if(result.has_value())
            capture.args.push_back(*result);
        else
            capture.errors.push_back(result.error());
    }
    EXPECT(capture.errors.empty());
    ASSERT(capture.args.size() == 1U);
    EXPECT(capture.args[0].id == FILTER_OPT_HIDDEN);
}

ZEST_CASE(flag_filter_affects_matching) {
    auto table = make_filter_opt_table();

    auto argv = split2vec("--flagged");
    ParseCapture include_capture;
    ParseOptions include_opts;
    include_opts.include_flags = kExperimentalFlag;
    for(auto& result: table.parse(argv, include_opts)) {
        if(result.has_value())
            include_capture.args.push_back(*result);
        else
            include_capture.errors.push_back(result.error());
    }
    EXPECT(include_capture.errors.empty());
    ASSERT(include_capture.args.size() == 1U);
    EXPECT(include_capture.args[0].id == FILTER_OPT_FLAGGED);

    argv = split2vec("--flagged");
    ParseCapture exclude_capture;
    ParseOptions exclude_opts;
    exclude_opts.exclude_flags = kExperimentalFlag;
    for(auto& result: table.parse(argv, exclude_opts)) {
        if(result.has_value())
            exclude_capture.args.push_back(*result);
        else
            exclude_capture.errors.push_back(result.error());
    }
    EXPECT(exclude_capture.errors.empty());
    ASSERT(exclude_capture.args.size() == 1U);
    EXPECT(exclude_capture.args[0].id == FILTER_OPT_UNKNOWN);
}

ZEST_CASE(option_kinds_parse_correctly) {
    auto table = make_kinds_opt_table();

    auto parsed = parse_all(table, split2vec("-jabc"));
    EXPECT(parsed.errors.empty());
    ASSERT(parsed.args.size() == 1U);
    EXPECT(parsed.args[0].id == KINDS_OPT_JOINED);
    ASSERT(parsed.args[0].values.size() == 1U);
    EXPECT(parsed.args[0].values[0] == "abc");

    parsed = parse_all(table, split2vec("--list=a,,b,c"));
    EXPECT(parsed.errors.empty());
    ASSERT(parsed.args.size() == 1U);
    EXPECT(parsed.args[0].id == KINDS_OPT_COMMA_JOINED);
    ASSERT(parsed.args[0].values.size() == 3U);
    EXPECT(parsed.args[0].values[0] == "=a");
    EXPECT(parsed.args[0].values[1] == "b");
    EXPECT(parsed.args[0].values[2] == "c");

    parsed = parse_all(table, split2vec("--pair left right"));
    EXPECT(parsed.errors.empty());
    ASSERT(parsed.args.size() == 1U);
    EXPECT(parsed.args[0].id == KINDS_OPT_MULTI_ARG);
    ASSERT(parsed.args[0].values.size() == 2U);
    EXPECT(parsed.args[0].values[0] == "left");
    EXPECT(parsed.args[0].values[1] == "right");

    parsed = parse_all(table, split2vec("-o2"));
    EXPECT(parsed.errors.empty());
    ASSERT(parsed.args.size() == 1U);
    EXPECT(parsed.args[0].id == KINDS_OPT_JOINED_OR_SEPARATE);
    ASSERT(parsed.args[0].values.size() == 1U);
    EXPECT(parsed.args[0].values[0] == "2");

    parsed = parse_all(table, split2vec("-o 3"));
    EXPECT(parsed.errors.empty());
    ASSERT(parsed.args.size() == 1U);
    EXPECT(parsed.args[0].id == KINDS_OPT_JOINED_OR_SEPARATE);
    ASSERT(parsed.args[0].values.size() == 1U);
    EXPECT(parsed.args[0].values[0] == "3");

    parsed = parse_all(table, split2vec("-x4 tail"));
    EXPECT(parsed.errors.empty());
    ASSERT(parsed.args.size() == 1U);
    EXPECT(parsed.args[0].id == KINDS_OPT_JOINED_AND_SEPARATE);
    ASSERT(parsed.args[0].values.size() == 2U);
    EXPECT(parsed.args[0].values[0] == "4");
    EXPECT(parsed.args[0].values[1] == "tail");

    parsed = parse_all(table, split2vec("--rest one two"));
    EXPECT(parsed.errors.empty());
    ASSERT(parsed.args.size() == 1U);
    EXPECT(parsed.args[0].id == KINDS_OPT_REMAINING);
    ASSERT(parsed.args[0].values.size() == 2U);
    EXPECT(parsed.args[0].values[0] == "one");
    EXPECT(parsed.args[0].values[1] == "two");

    parsed = parse_all(table, split2vec("--tailz one two"));
    EXPECT(parsed.errors.empty());
    ASSERT(parsed.args.size() == 1U);
    EXPECT(parsed.args[0].id == KINDS_OPT_REMAINING_JOINED);
    ASSERT(parsed.args[0].values.size() == 3U);
    EXPECT(parsed.args[0].values[0] == "z");
    EXPECT(parsed.args[0].values[1] == "one");
    EXPECT(parsed.args[0].values[2] == "two");
}

ZEST_CASE(option_matches_and_render_style) {
    auto table = make_match_opt_table();

    const auto member = table.option(MATCH_OPT_MEMBER);
    const auto alias = table.option(MATCH_OPT_ALIAS_MEMBER);
    EXPECT(member->matches(MATCH_OPT_GROUP));
    EXPECT(alias->matches(MATCH_OPT_GROUP));
    EXPECT(alias->unaliased_option().id() == MATCH_OPT_MEMBER);

    EXPECT(table.option(MATCH_OPT_JOINED)->render_style() == RenderStyle::Joined);
    EXPECT(table.option(MATCH_OPT_MEMBER)->render_style() == RenderStyle::Separate);
    EXPECT(table.option(MATCH_OPT_OVERRIDE_FLAG)->render_style() == RenderStyle::Joined);
}

ZEST_CASE(flag_aliases_merge_values) {
    auto table = make_alias_opt_table();
    auto parsed = parse_all(table, split2vec("--trap-defaults --emit-llvm"));

    EXPECT(parsed.errors.empty());
    ASSERT(parsed.args.size() == 2U);

    EXPECT(parsed.args[0].id == ALIAS_OPT_TRAP_EQ);
    ASSERT(parsed.args[0].values.size() == 2U);
    EXPECT(parsed.args[0].values[0] == "all");
    EXPECT(parsed.args[0].values[1] == "undefined");

    EXPECT(parsed.args[1].id == ALIAS_OPT_EMIT_EQ);
    ASSERT(parsed.args[1].values.size() == 1U);
    EXPECT(parsed.args[1].values[0] == "");
}

ZEST_CASE(find_option_basic) {
    auto table = make_main_opt_table();

    EXPECT(table.find_option("--help")->id() == MAIN_OPT_HELP);
    EXPECT(table.find_option("-h")->id() == MAIN_OPT_HELP);
    EXPECT(table.find_option("-s")->id() == MAIN_OPT_SCRIPT);
    EXPECT(!table.find_option("--nonexistent"));
}

};  // ZEST_SUITE(option_extended_coverage)

ZEST_SUITE(option_render) {

auto collect(const OptTable& table, const ParsedArg& arg) {
    std::vector<std::string> out;
    auto cb = [&](std::string_view sv) {
        out.push_back(std::string(sv));
    };
    table.render(arg, cb);
    return out;
}

ZEST_CASE(render_flag_separate) {
    auto table = make_main_opt_table();
    auto parsed = parse_all(table, split2vec("--help"));
    ASSERT(parsed.args.size() == 1U);

    auto rendered = collect(table, parsed.args[0]);
    ASSERT(rendered.size() == 1U);
    EXPECT(rendered[0] == "--help");
}

ZEST_CASE(render_alias_uses_canonical_name) {
    auto table = make_main_opt_table();
    auto parsed = parse_all(table, split2vec("-h"));
    ASSERT(parsed.args.size() == 1U);
    EXPECT(parsed.args[0].id == MAIN_OPT_HELP);

    auto rendered = collect(table, parsed.args[0]);
    ASSERT(rendered.size() == 1U);
    EXPECT(rendered[0] == "--help");
}

ZEST_CASE(render_separate_option) {
    auto table = make_main_opt_table();
    auto opts = make_main_parse_options();
    auto parsed = parse_all(table, split2vec("-s script.py"), opts);
    ASSERT(parsed.args.size() == 1U);

    auto rendered = collect(table, parsed.args[0]);
    ASSERT(rendered.size() == 2U);
    EXPECT(rendered[0] == "-s");
    EXPECT(rendered[1] == "script.py");
}

ZEST_CASE(render_joined_option) {
    auto table = make_kinds_opt_table();
    auto parsed = parse_all(table, split2vec("-jabc"));
    ASSERT(parsed.args.size() == 1U);

    auto rendered = collect(table, parsed.args[0]);
    ASSERT(rendered.size() == 1U);
    EXPECT(rendered[0] == "-jabc");
}

ZEST_CASE(render_comma_joined_option) {
    auto table = make_kinds_opt_table();
    auto parsed = parse_all(table, split2vec("--list=a,b,c"));
    ASSERT(parsed.args.size() == 1U);

    auto rendered = collect(table, parsed.args[0]);
    ASSERT(rendered.size() == 1U);
    EXPECT(rendered[0] == "--list=a,b,c");
}

ZEST_CASE(render_multi_arg_option) {
    auto table = make_kinds_opt_table();
    auto parsed = parse_all(table, split2vec("--pair left right"));
    ASSERT(parsed.args.size() == 1U);

    auto rendered = collect(table, parsed.args[0]);
    ASSERT(rendered.size() == 3U);
    EXPECT(rendered[0] == "--pair");
    EXPECT(rendered[1] == "left");
    EXPECT(rendered[2] == "right");
}

ZEST_CASE(render_joined_or_separate_as_joined) {
    auto table = make_kinds_opt_table();
    auto parsed = parse_all(table, split2vec("-o2"));
    ASSERT(parsed.args.size() == 1U);

    auto rendered = collect(table, parsed.args[0]);
    ASSERT(rendered.size() == 2U);
    EXPECT(rendered[0] == "-o");
    EXPECT(rendered[1] == "2");
}

ZEST_CASE(render_joined_and_separate) {
    auto table = make_kinds_opt_table();
    auto parsed = parse_all(table, split2vec("-x4 tail"));
    ASSERT(parsed.args.size() == 1U);

    auto rendered = collect(table, parsed.args[0]);
    ASSERT(rendered.size() == 2U);
    EXPECT(rendered[0] == "-x4");
    EXPECT(rendered[1] == "tail");
}

ZEST_CASE(render_input_preserves_spelling) {
    auto table = make_main_opt_table();
    auto opts = make_main_parse_options();
    auto parsed = parse_all(table, split2vec("myfile.txt"), opts);
    ASSERT(parsed.args.size() == 1U);
    EXPECT(parsed.args[0].id == MAIN_OPT_INPUT);

    auto rendered = collect(table, parsed.args[0]);
    ASSERT(rendered.size() == 1U);
    EXPECT(rendered[0] == "myfile.txt");
}

ZEST_CASE(render_unknown_preserves_spelling) {
    auto table = make_main_opt_table();
    auto opts = make_main_parse_options();
    auto parsed = parse_all(table, split2vec("--unknown-flag"), opts);
    ASSERT(parsed.args.size() == 1U);
    EXPECT(parsed.args[0].id == MAIN_OPT_UNKNOWN);

    auto rendered = collect(table, parsed.args[0]);
    ASSERT(rendered.size() == 1U);
    EXPECT(rendered[0] == "--unknown-flag");
}

ZEST_CASE(render_alias_with_args) {
    auto table = make_alias_opt_table();
    auto parsed = parse_all(table, split2vec("--trap-defaults"));
    ASSERT(parsed.args.size() == 1U);
    EXPECT(parsed.args[0].id == ALIAS_OPT_TRAP_EQ);

    auto rendered = collect(table, parsed.args[0]);
    ASSERT(rendered.size() == 1U);
    EXPECT(rendered[0] == "--trap=all,undefined");
}

ZEST_CASE(render_override_flag_uses_joined_style) {
    auto table = make_match_opt_table();
    auto parsed = parse_all(table, split2vec("-r"));
    ASSERT(parsed.args.size() == 1U);

    auto rendered = collect(table, parsed.args[0]);
    ASSERT(rendered.size() == 1U);
    EXPECT(rendered[0] == "-r");
}

};  // ZEST_SUITE(option_render)

}  // namespace
}  // namespace kota::option
