#include <array>
#include <cassert>
#include <expected>
#include <format>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "eventide/option/opt_table.h"
#include "eventide/option/option.h"
#include "eventide/option/util.h"
#include "eventide/zest/zest.h"

namespace {

namespace opt = eventide::option;
using namespace std::literals::string_view_literals;

std::vector<std::string> split2vec(std::string_view str) {
    auto views = std::views::split(str, ' ') | std::views::transform([](auto&& rng) {
                     return std::string(rng.begin(), rng.end());
                 });
    return std::vector<std::string>(views.begin(), views.end());
}

struct ParseCapture {
    std::vector<opt::ParsedArgumentOwning> args;
    std::vector<std::string> errors;
};

ParseCapture parse_all(const opt::OptTable& table, std::vector<std::string> argv) {
    ParseCapture capture;
    table.parse_args(argv, [&](std::expected<opt::ParsedArgument, std::string> parsed) {
        if(parsed.has_value()) {
            capture.args.emplace_back(opt::ParsedArgumentOwning::from_parsed_argument(*parsed));
        } else {
            capture.errors.emplace_back(parsed.error());
        }
    });
    return capture;
}

enum MainOptionID {
    MAIN_OPT_INVALID = 0,
    MAIN_OPT_INPUT = 1,
    MAIN_OPT_UNKNOWN = 2,
    MAIN_OPT_HELP,
    MAIN_OPT_HELP_SHORT,
    MAIN_OPT_SCRIPT,
};

constexpr auto kMainOptInfos = std::array{
    opt::OptTable::Info::input(MAIN_OPT_INPUT),
    opt::OptTable::Info::unknown(MAIN_OPT_UNKNOWN),
    opt::OptTable::Info::unaliased_one(opt::pfx_double,
                                       "--help",
                                       MAIN_OPT_HELP,
                                       opt::Option::FlagClass,
                                       0,
                                       "Display help",
                                       ""),
    opt::OptTable::Info::unaliased_one(opt::pfx_dash,
                                       "-h",
                                       MAIN_OPT_HELP_SHORT,
                                       opt::Option::FlagClass,
                                       0,
                                       "Display help",
                                       "")
        .alias_of(MAIN_OPT_HELP),
    opt::OptTable::Info::unaliased_one(opt::pfx_dash,
                                       "-s",
                                       MAIN_OPT_SCRIPT,
                                       opt::Option::SeparateClass,
                                       1,
                                       "Script path",
                                       ""),
};

opt::OptTable make_main_opt_table() {
    return opt::OptTable(std::span<const opt::OptTable::Info>(kMainOptInfos))
        .set_tablegen_mode(false)
        .set_dash_dash_parsing(true)
        .set_dash_dash_as_single_pack(true);
}

enum ProxyOptionID {
    PROXY_OPT_INVALID = 0,
    PROXY_OPT_INPUT = 1,
    PROXY_OPT_UNKNOWN = 2,
    PROXY_OPT_PARENT_ID,
    PROXY_OPT_EXEC,
};

constexpr auto kProxyOptInfos = std::array{
    opt::OptTable::Info::input(PROXY_OPT_INPUT),
    opt::OptTable::Info::unknown(PROXY_OPT_UNKNOWN),
    opt::OptTable::Info::unaliased_one(opt::pfx_dash,
                                       "-p",
                                       PROXY_OPT_PARENT_ID,
                                       opt::Option::SeparateClass,
                                       1,
                                       "Parent process id",
                                       ""),
    opt::OptTable::Info::unaliased_one(opt::pfx_dash_double,
                                       "--exec",
                                       PROXY_OPT_EXEC,
                                       opt::Option::SeparateClass,
                                       1,
                                       "Exec",
                                       ""),
};

opt::OptTable make_proxy_opt_table() {
    return opt::OptTable(std::span<const opt::OptTable::Info>(kProxyOptInfos))
        .set_tablegen_mode(false)
        .set_dash_dash_parsing(true)
        .set_dash_dash_as_single_pack(true);
}

struct ProxyParsedOption {
    std::string parent_id;
    std::string executable;
    std::expected<std::vector<std::string>, std::string> argv = std::vector<std::string>{};
};

ProxyParsedOption parse_proxy_opt(std::span<std::string> argv_span, bool with_program_name = true) {
    ProxyParsedOption option{};
    auto argv = with_program_name ? argv_span.subspan(1) : argv_span;
#if ETD_ENABLE_EXCEPTIONS
    if(argv.empty()) {
        throw std::invalid_argument("no arguments provided");
    }
#else
    if(argv.empty()) {
        option.argv = std::unexpected(std::string("no arguments provided"));
        return option;
    }
#endif

    std::string error;
    auto table = make_proxy_opt_table();
    table.parse_args(argv, [&](const std::expected<opt::ParsedArgument, std::string>& arg) {
        if(!error.empty()) {
            return;
        }
        if(!arg.has_value()) {
            error = std::format("error parsing arguments: {}", arg.error());
            return;
        }

        switch(arg->option_id.id()) {
            case PROXY_OPT_PARENT_ID: option.parent_id = arg->values[0]; break;
            case PROXY_OPT_EXEC: option.executable = arg->values[0]; break;
            case PROXY_OPT_INPUT:
                if(arg->get_spelling_view() == "--") {
                    option.argv = std::vector<std::string>(arg->values.begin(), arg->values.end());
                } else {
                    option.argv = std::unexpected(
                        std::format("error from hook: {}", arg->get_spelling_view()));
                }
                break;
            default: error = std::format("unknown argument: {}", argv[arg->index]); break;
        }
    });

    if(!error.empty()) {
#if ETD_ENABLE_EXCEPTIONS
        throw std::invalid_argument(error);
#else
        option.argv = std::unexpected(error);
#endif
    }
    return option;
}

ProxyParsedOption parse_proxy_opt(int argc, char* argv[], bool with_program_name = true) {
    std::vector<std::string> argv_vec;
    argv_vec.reserve(argc);
    for(int i = 0; i < argc; ++i) {
        argv_vec.emplace_back(argv[i] != nullptr ? argv[i] : "");
    }
    return parse_proxy_opt(std::span<std::string>(argv_vec), with_program_name);
}

TEST_SUITE(option_catter_migration) {

TEST_CASE(main_option_table_has_expected_options) {
    auto table = make_main_opt_table();
    auto parsed = parse_all(
        table,
        split2vec("-p 1234 -s script::profile --dest=114514 -- /usr/bin/clang++ --version"));

    EXPECT_TRUE(parsed.errors.empty());
    ASSERT_EQ(parsed.args.size(), 5U);

    EXPECT_EQ(parsed.args[0].option_id.id(), MAIN_OPT_UNKNOWN);
    EXPECT_EQ(parsed.args[0].get_spelling_view(), "-p");
    EXPECT_EQ(parsed.args[0].index, 0U);

    EXPECT_EQ(parsed.args[1].option_id.id(), MAIN_OPT_INPUT);
    EXPECT_EQ(parsed.args[1].get_spelling_view(), "1234");
    EXPECT_EQ(parsed.args[1].index, 1U);

    EXPECT_EQ(parsed.args[2].option_id.id(), MAIN_OPT_SCRIPT);
    ASSERT_EQ(parsed.args[2].values.size(), 1U);
    EXPECT_EQ(parsed.args[2].values[0], "script::profile");
    EXPECT_EQ(parsed.args[2].get_spelling_view(), "-s");
    EXPECT_EQ(parsed.args[2].index, 2U);

    EXPECT_EQ(parsed.args[3].option_id.id(), MAIN_OPT_UNKNOWN);
    EXPECT_EQ(parsed.args[3].get_spelling_view(), "--dest=114514");
    EXPECT_EQ(parsed.args[3].index, 4U);

    EXPECT_EQ(parsed.args[4].option_id.id(), MAIN_OPT_INPUT);
    EXPECT_EQ(parsed.args[4].get_spelling_view(), "--");
    ASSERT_EQ(parsed.args[4].values.size(), 2U);
    EXPECT_EQ(parsed.args[4].values[0], "/usr/bin/clang++");
    EXPECT_EQ(parsed.args[4].values[1], "--version");
    EXPECT_EQ(parsed.args[4].index, 5U);

    parsed = parse_all(table, split2vec("-h"));
    EXPECT_TRUE(parsed.errors.empty());
    ASSERT_EQ(parsed.args.size(), 1U);
    EXPECT_EQ(parsed.args[0].option_id.id(), MAIN_OPT_HELP_SHORT);
    EXPECT_EQ(parsed.args[0].get_spelling_view(), "-h");
    EXPECT_EQ(parsed.args[0].values.size(), 0U);
    EXPECT_EQ(parsed.args[0].index, 0U);
    EXPECT_EQ(parsed.args[0].unaliased_opt().id(), MAIN_OPT_HELP);
}

TEST_CASE(proxy_option_table_has_expected_options) {
    auto table = make_proxy_opt_table();

    auto parsed = parse_all(table, split2vec("-p 1234"));
    EXPECT_TRUE(parsed.errors.empty());
    ASSERT_EQ(parsed.args.size(), 1U);
    EXPECT_EQ(parsed.args[0].option_id.id(), PROXY_OPT_PARENT_ID);
    ASSERT_EQ(parsed.args[0].values.size(), 1U);
    EXPECT_EQ(parsed.args[0].values[0], "1234");
    EXPECT_EQ(parsed.args[0].get_spelling_view(), "-p");
    EXPECT_EQ(parsed.args[0].index, 0U);

    parsed = parse_all(table, split2vec("--exec /bin/ls"));
    EXPECT_TRUE(parsed.errors.empty());
    ASSERT_EQ(parsed.args.size(), 1U);
    EXPECT_EQ(parsed.args[0].option_id.id(), PROXY_OPT_EXEC);
    ASSERT_EQ(parsed.args[0].values.size(), 1U);
    EXPECT_EQ(parsed.args[0].values[0], "/bin/ls");
    EXPECT_EQ(parsed.args[0].get_spelling_view(), "--exec");
    EXPECT_EQ(parsed.args[0].index, 0U);

    parsed = parse_all(table, split2vec("-p 12 --exec /usr/bin/clang++ -- clang++ --version"));
    EXPECT_TRUE(parsed.errors.empty());
    ASSERT_EQ(parsed.args.size(), 3U);

    EXPECT_EQ(parsed.args[0].option_id.id(), PROXY_OPT_PARENT_ID);
    ASSERT_EQ(parsed.args[0].values.size(), 1U);
    EXPECT_EQ(parsed.args[0].values[0], "12");
    EXPECT_EQ(parsed.args[0].index, 0U);

    EXPECT_EQ(parsed.args[1].option_id.id(), PROXY_OPT_EXEC);
    ASSERT_EQ(parsed.args[1].values.size(), 1U);
    EXPECT_EQ(parsed.args[1].values[0], "/usr/bin/clang++");
    EXPECT_EQ(parsed.args[1].index, 2U);

    EXPECT_EQ(parsed.args[2].option_id.id(), PROXY_OPT_INPUT);
    EXPECT_EQ(parsed.args[2].get_spelling_view(), "--");
    ASSERT_EQ(parsed.args[2].values.size(), 2U);
    EXPECT_EQ(parsed.args[2].values[0], "clang++");
    EXPECT_EQ(parsed.args[2].values[1], "--version");
    EXPECT_EQ(parsed.args[2].index, 4U);
}

TEST_CASE(proxy_unknown_and_missing_value) {
    auto table = make_proxy_opt_table();

    auto parsed = parse_all(table, split2vec("--unknown-option value"));
    EXPECT_TRUE(parsed.errors.empty());
    ASSERT_EQ(parsed.args.size(), 2U);

    EXPECT_EQ(parsed.args[0].option_id.id(), PROXY_OPT_UNKNOWN);
    EXPECT_EQ(parsed.args[0].get_spelling_view(), "--unknown-option");
    EXPECT_EQ(parsed.args[0].values.size(), 0U);
    EXPECT_EQ(parsed.args[0].index, 0U);

    EXPECT_EQ(parsed.args[1].option_id.id(), PROXY_OPT_INPUT);
    EXPECT_EQ(parsed.args[1].get_spelling_view(), "value");
    EXPECT_EQ(parsed.args[1].values.size(), 0U);
    EXPECT_EQ(parsed.args[1].index, 1U);

    parsed = parse_all(table, split2vec("-p"));
    EXPECT_EQ(parsed.args.size(), 0U);
    ASSERT_EQ(parsed.errors.size(), 1U);
    EXPECT_TRUE(parsed.errors[0].contains("missing argument value"));
}

TEST_CASE(proxy_parser_parse_opt_success) {
    std::array<std::string, 5> argv_storage = {"", "-p", "5678", "--exec", "/bin/echo"};
    std::array<char*, 5> argv{};
    for(size_t i = 0; i < argv_storage.size(); ++i) {
        argv[i] = argv_storage[i].data();
    }

    auto f = [&]() {
        auto result = parse_proxy_opt(static_cast<int>(argv.size()), argv.data());
        EXPECT_EQ(result.parent_id, "5678");
        EXPECT_EQ(result.executable, "/bin/echo");
        EXPECT_TRUE(result.argv.has_value());
        EXPECT_EQ(result.argv.value().size(), 0U);
    };
#if ETD_ENABLE_EXCEPTIONS
    EXPECT_NOTHROWS(f());
#else
    f();
#endif
}

TEST_CASE(proxy_parser_parse_opt_with_input_args) {
    auto argv = split2vec("proxy -p 91011 --exec /usr/bin/python3 -- script.py --verbose");
    auto f = [&]() {
        auto result = parse_proxy_opt(argv);
        EXPECT_EQ(result.parent_id, "91011");
        EXPECT_EQ(result.executable, "/usr/bin/python3");
        EXPECT_TRUE(result.argv.has_value());
        ASSERT_EQ(result.argv.value().size(), 2U);
        EXPECT_EQ(result.argv.value()[0], "script.py");
        EXPECT_EQ(result.argv.value()[1], "--verbose");
    };
#if ETD_ENABLE_EXCEPTIONS
    EXPECT_NOTHROWS(f());
#else
    f();
#endif
}

TEST_CASE(proxy_parser_parse_opt_error_handling) {
    auto argv = split2vec("proxy -p");
#if ETD_ENABLE_EXCEPTIONS
    EXPECT_THROWS((parse_proxy_opt(argv)));
#else
    auto result = parse_proxy_opt(argv);
    EXPECT_FALSE(result.argv.has_value());
    EXPECT_TRUE(std::string_view(result.argv.error()).contains("missing argument value"));
#endif
}

TEST_CASE(proxy_parser_parse_opt_passes_error_payload) {
    auto argv = split2vec("proxy -p 91011 --exec /usr/bin/python3 report err!");
    auto f = [&]() {
        auto result = parse_proxy_opt(argv);
        EXPECT_FALSE(result.argv.has_value());
    };
#if ETD_ENABLE_EXCEPTIONS
    EXPECT_NOTHROWS(f());
#else
    f();
#endif
}

};  // TEST_SUITE(option_catter_migration)

enum GroupedOptionID {
    GROUPED_OPT_INVALID = 0,
    GROUPED_OPT_INPUT = 1,
    GROUPED_OPT_UNKNOWN = 2,
    GROUPED_OPT_A,
    GROUPED_OPT_B,
};

constexpr auto kGroupedOptInfos = std::array{
    opt::OptTable::Info::input(GROUPED_OPT_INPUT),
    opt::OptTable::Info::unknown(GROUPED_OPT_UNKNOWN),
    opt::OptTable::Info::unaliased_one(opt::pfx_dash,
                                       "-a",
                                       GROUPED_OPT_A,
                                       opt::Option::FlagClass,
                                       0,
                                       "",
                                       ""),
    opt::OptTable::Info::unaliased_one(opt::pfx_dash,
                                       "-b",
                                       GROUPED_OPT_B,
                                       opt::Option::FlagClass,
                                       0,
                                       "",
                                       ""),
};

opt::OptTable make_grouped_opt_table() {
    return opt::OptTable(std::span<const opt::OptTable::Info>(kGroupedOptInfos))
        .set_tablegen_mode(false)
        .set_grouped_short_options(true);
}

enum IgnoreCaseOptionID {
    IGNORE_CASE_OPT_INVALID = 0,
    IGNORE_CASE_OPT_INPUT = 1,
    IGNORE_CASE_OPT_UNKNOWN = 2,
    IGNORE_CASE_OPT_HELP,
};

constexpr auto kIgnoreCaseOptInfos = std::array{
    opt::OptTable::Info::input(IGNORE_CASE_OPT_INPUT),
    opt::OptTable::Info::unknown(IGNORE_CASE_OPT_UNKNOWN),
    opt::OptTable::Info::unaliased_one(opt::pfx_double,
                                       "--help",
                                       IGNORE_CASE_OPT_HELP,
                                       opt::Option::FlagClass,
                                       0,
                                       "",
                                       ""),
};

opt::OptTable make_ignore_case_opt_table(bool ignore_case) {
    auto table = opt::OptTable(std::span<const opt::OptTable::Info>(kIgnoreCaseOptInfos));
    table.set_tablegen_mode(false);
    table.set_ignore_case(ignore_case);
    return table;
}

enum FilterOptionID {
    FILTER_OPT_INVALID = 0,
    FILTER_OPT_INPUT = 1,
    FILTER_OPT_UNKNOWN = 2,
    FILTER_OPT_PUBLIC,
    FILTER_OPT_HIDDEN,
    FILTER_OPT_FLAGGED,
};

constexpr unsigned kInternalVisibility = 1U << 1;
constexpr unsigned kExperimentalFlag = 1U << 6;

constexpr auto kFilterOptInfos = std::array{
    opt::OptTable::Info::input(FILTER_OPT_INPUT),
    opt::OptTable::Info::unknown(FILTER_OPT_UNKNOWN),
    opt::OptTable::Info::unaliased_one(opt::pfx_double,
                                       "--public",
                                       FILTER_OPT_PUBLIC,
                                       opt::Option::FlagClass,
                                       0,
                                       "",
                                       ""),
    opt::OptTable::Info::unaliased_one(opt::pfx_double,
                                       "--hidden",
                                       FILTER_OPT_HIDDEN,
                                       opt::Option::FlagClass,
                                       0,
                                       "",
                                       "",
                                       0,
                                       0,
                                       kInternalVisibility),
    opt::OptTable::Info::unaliased_one(opt::pfx_double,
                                       "--flagged",
                                       FILTER_OPT_FLAGGED,
                                       opt::Option::FlagClass,
                                       0,
                                       "",
                                       "",
                                       0,
                                       kExperimentalFlag,
                                       opt::DefaultVis),
};

opt::OptTable make_filter_opt_table() {
    return opt::OptTable(std::span<const opt::OptTable::Info>(kFilterOptInfos))
        .set_tablegen_mode(false);
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
    opt::OptTable::Info::input(KINDS_OPT_INPUT),
    opt::OptTable::Info::unknown(KINDS_OPT_UNKNOWN),
    opt::OptTable::Info::unaliased_one(opt::pfx_dash,
                                       "-j",
                                       KINDS_OPT_JOINED,
                                       opt::Option::JoinedClass,
                                       1,
                                       "",
                                       ""),
    opt::OptTable::Info::unaliased_one(opt::pfx_double,
                                       "--list",
                                       KINDS_OPT_COMMA_JOINED,
                                       opt::Option::CommaJoinedClass,
                                       1,
                                       "",
                                       ""),
    opt::OptTable::Info::unaliased_one(opt::pfx_double,
                                       "--pair",
                                       KINDS_OPT_MULTI_ARG,
                                       opt::Option::MultiArgClass,
                                       2,
                                       "",
                                       ""),
    opt::OptTable::Info::unaliased_one(opt::pfx_dash,
                                       "-o",
                                       KINDS_OPT_JOINED_OR_SEPARATE,
                                       opt::Option::JoinedOrSeparateClass,
                                       1,
                                       "",
                                       ""),
    opt::OptTable::Info::unaliased_one(opt::pfx_dash,
                                       "-x",
                                       KINDS_OPT_JOINED_AND_SEPARATE,
                                       opt::Option::JoinedAndSeparateClass,
                                       2,
                                       "",
                                       ""),
    opt::OptTable::Info::unaliased_one(opt::pfx_double,
                                       "--rest",
                                       KINDS_OPT_REMAINING,
                                       opt::Option::RemainingArgsClass,
                                       0,
                                       "",
                                       ""),
    opt::OptTable::Info::unaliased_one(opt::pfx_double,
                                       "--tail",
                                       KINDS_OPT_REMAINING_JOINED,
                                       opt::Option::RemainingArgsJoinedClass,
                                       0,
                                       "",
                                       ""),
};

opt::OptTable make_kinds_opt_table() {
    return opt::OptTable(std::span<const opt::OptTable::Info>(kKindsOptInfos))
        .set_tablegen_mode(false);
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
    opt::OptTable::Info::input(MATCH_OPT_INPUT),
    opt::OptTable::Info::unknown(MATCH_OPT_UNKNOWN),
    opt::OptTable::Info::unaliased_one(opt::pfx_none,
                                       "group",
                                       MATCH_OPT_GROUP,
                                       opt::Option::GroupClass,
                                       0,
                                       "",
                                       ""),
    opt::OptTable::Info::unaliased_one(opt::pfx_dash,
                                       "-m",
                                       MATCH_OPT_MEMBER,
                                       opt::Option::FlagClass,
                                       0,
                                       "",
                                       "",
                                       MATCH_OPT_GROUP),
    opt::OptTable::Info::unaliased_one(opt::pfx_dash,
                                       "-am",
                                       MATCH_OPT_ALIAS_MEMBER,
                                       opt::Option::FlagClass,
                                       0,
                                       "",
                                       "")
        .alias_of(MATCH_OPT_MEMBER),
    opt::OptTable::Info::unaliased_one(opt::pfx_dash,
                                       "-j",
                                       MATCH_OPT_JOINED,
                                       opt::Option::JoinedClass,
                                       1,
                                       "",
                                       ""),
    opt::OptTable::Info::unaliased_one(opt::pfx_dash,
                                       "-r",
                                       MATCH_OPT_OVERRIDE_FLAG,
                                       opt::Option::FlagClass,
                                       0,
                                       "",
                                       "",
                                       0,
                                       opt::RenderJoined),
};

opt::OptTable make_match_opt_table() {
    return opt::OptTable(std::span<const opt::OptTable::Info>(kMatchOptInfos))
        .set_tablegen_mode(false);
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
    opt::OptTable::Info::input(ALIAS_OPT_INPUT),
    opt::OptTable::Info::unknown(ALIAS_OPT_UNKNOWN),
    opt::OptTable::Info::unaliased_one(opt::pfx_double,
                                       "--trap=",
                                       ALIAS_OPT_TRAP_EQ,
                                       opt::Option::CommaJoinedClass,
                                       1,
                                       "",
                                       ""),
    opt::OptTable::Info::unaliased_one(opt::pfx_double,
                                       "--trap-defaults",
                                       ALIAS_OPT_TRAP_DEFAULTS,
                                       opt::Option::FlagClass,
                                       0,
                                       "",
                                       "")
        .alias_of(ALIAS_OPT_TRAP_EQ, "all\0undefined\0"),
    opt::OptTable::Info::unaliased_one(opt::pfx_double,
                                       "--emit=",
                                       ALIAS_OPT_EMIT_EQ,
                                       opt::Option::JoinedClass,
                                       1,
                                       "",
                                       ""),
    opt::OptTable::Info::unaliased_one(opt::pfx_double,
                                       "--emit-llvm",
                                       ALIAS_OPT_EMIT_LLVM,
                                       opt::Option::FlagClass,
                                       0,
                                       "",
                                       "")
        .alias_of(ALIAS_OPT_EMIT_EQ),
};

opt::OptTable make_alias_opt_table() {
    return opt::OptTable(std::span<const opt::OptTable::Info>(kAliasOptInfos))
        .set_tablegen_mode(false);
}

TEST_SUITE(option_extended_coverage) {

TEST_CASE(ignore_case_controls_matching) {
    auto strict_table = make_ignore_case_opt_table(false);
    auto parsed = parse_all(strict_table, split2vec("--HELP"));
    EXPECT_TRUE(parsed.errors.empty());
    ASSERT_EQ(parsed.args.size(), 1U);
    EXPECT_EQ(parsed.args[0].option_id.id(), IGNORE_CASE_OPT_UNKNOWN);

    auto ignore_case_table = make_ignore_case_opt_table(true);
    parsed = parse_all(ignore_case_table, split2vec("--HELP"));
    EXPECT_TRUE(parsed.errors.empty());
    ASSERT_EQ(parsed.args.size(), 1U);
    EXPECT_EQ(parsed.args[0].option_id.id(), IGNORE_CASE_OPT_HELP);
}

TEST_CASE(grouped_short_option_parsing_paths) {
    auto table = make_grouped_opt_table();

    auto argv = split2vec("-ab");
    unsigned index = 0;
    auto first = table.parse_one_arg_grouped(argv, index);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->option_id.id(), GROUPED_OPT_A);
    EXPECT_EQ(first->get_spelling_view(), "-a");
    EXPECT_EQ(index, 0U);
    EXPECT_EQ(argv[0], "-b");

    auto second = table.parse_one_arg_grouped(argv, index);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->option_id.id(), GROUPED_OPT_B);
    EXPECT_EQ(second->get_spelling_view(), "-b");
    EXPECT_EQ(index, 1U);

    argv = split2vec("-a=1");
    index = 0;
    auto equals_form = table.parse_one_arg_grouped(argv, index);
    ASSERT_TRUE(equals_form.has_value());
    EXPECT_EQ(equals_form->option_id.id(), GROUPED_OPT_UNKNOWN);
    EXPECT_EQ(equals_form->get_spelling_view(), "-a=1");
    EXPECT_EQ(index, 1U);

    argv = split2vec("-zx");
    index = 0;
    auto unknown_split = table.parse_one_arg_grouped(argv, index);
    ASSERT_TRUE(unknown_split.has_value());
    EXPECT_EQ(unknown_split->option_id.id(), GROUPED_OPT_UNKNOWN);
    EXPECT_EQ(unknown_split->get_spelling_view(), "-z");
    EXPECT_EQ(index, 0U);
    EXPECT_EQ(argv[0], "-x");
}

TEST_CASE(visibility_and_flag_filters_affect_matching) {
    auto table = make_filter_opt_table();
    unsigned missing_arg_index = 0;
    unsigned missing_arg_count = 0;
    std::vector<unsigned> ids;

    auto argv = split2vec("--hidden");
    table.parse_args(
        argv,
        missing_arg_index,
        missing_arg_count,
        [&](const opt::ParsedArgument& parsed) { ids.push_back(parsed.option_id.id()); },
        opt::Visibility(opt::DefaultVis));
    EXPECT_EQ(missing_arg_count, 0U);
    ASSERT_EQ(ids.size(), 1U);
    EXPECT_EQ(ids[0], FILTER_OPT_UNKNOWN);

    ids.clear();
    table.parse_args(
        argv,
        missing_arg_index,
        missing_arg_count,
        [&](const opt::ParsedArgument& parsed) { ids.push_back(parsed.option_id.id()); },
        opt::Visibility(kInternalVisibility));
    EXPECT_EQ(missing_arg_count, 0U);
    ASSERT_EQ(ids.size(), 1U);
    EXPECT_EQ(ids[0], FILTER_OPT_HIDDEN);

    ids.clear();
    argv = split2vec("--flagged");
    table.parse_args(
        argv,
        missing_arg_index,
        missing_arg_count,
        [&](const opt::ParsedArgument& parsed) { ids.push_back(parsed.option_id.id()); },
        kExperimentalFlag,
        0);
    EXPECT_EQ(missing_arg_count, 0U);
    ASSERT_EQ(ids.size(), 1U);
    EXPECT_EQ(ids[0], FILTER_OPT_FLAGGED);

    ids.clear();
    table.parse_args(
        argv,
        missing_arg_index,
        missing_arg_count,
        [&](const opt::ParsedArgument& parsed) { ids.push_back(parsed.option_id.id()); },
        0,
        kExperimentalFlag);
    EXPECT_EQ(missing_arg_count, 0U);
    ASSERT_EQ(ids.size(), 1U);
    EXPECT_EQ(ids[0], FILTER_OPT_UNKNOWN);
}

TEST_CASE(parse_callback_can_stop_iteration) {
    auto table = make_filter_opt_table();
    auto argv = split2vec("--public --flagged");

    size_t callback_count = 0;
    bool first_was_value = false;
    table.parse_args(argv, [&](std::expected<opt::ParsedArgument, std::string> parsed) -> bool {
        ++callback_count;
        first_was_value = parsed.has_value();
        return false;
    });

    EXPECT_EQ(callback_count, 1U);
    EXPECT_TRUE(first_was_value);
}

TEST_CASE(option_kinds_parse_expected_values) {
    auto table = make_kinds_opt_table();

    auto parsed = parse_all(table, split2vec("-jabc"));
    EXPECT_TRUE(parsed.errors.empty());
    ASSERT_EQ(parsed.args.size(), 1U);
    EXPECT_EQ(parsed.args[0].option_id.id(), KINDS_OPT_JOINED);
    ASSERT_EQ(parsed.args[0].values.size(), 1U);
    EXPECT_EQ(parsed.args[0].values[0], "abc");

    parsed = parse_all(table, split2vec("--list=a,,b,c"));
    EXPECT_TRUE(parsed.errors.empty());
    ASSERT_EQ(parsed.args.size(), 1U);
    EXPECT_EQ(parsed.args[0].option_id.id(), KINDS_OPT_COMMA_JOINED);
    ASSERT_EQ(parsed.args[0].values.size(), 3U);
    EXPECT_EQ(parsed.args[0].values[0], "=a");
    EXPECT_EQ(parsed.args[0].values[1], "b");
    EXPECT_EQ(parsed.args[0].values[2], "c");

    parsed = parse_all(table, split2vec("--pair left right"));
    EXPECT_TRUE(parsed.errors.empty());
    ASSERT_EQ(parsed.args.size(), 1U);
    EXPECT_EQ(parsed.args[0].option_id.id(), KINDS_OPT_MULTI_ARG);
    ASSERT_EQ(parsed.args[0].values.size(), 2U);
    EXPECT_EQ(parsed.args[0].values[0], "left");
    EXPECT_EQ(parsed.args[0].values[1], "right");

    parsed = parse_all(table, split2vec("-o2"));
    EXPECT_TRUE(parsed.errors.empty());
    ASSERT_EQ(parsed.args.size(), 1U);
    EXPECT_EQ(parsed.args[0].option_id.id(), KINDS_OPT_JOINED_OR_SEPARATE);
    ASSERT_EQ(parsed.args[0].values.size(), 1U);
    EXPECT_EQ(parsed.args[0].values[0], "2");

    parsed = parse_all(table, split2vec("-o 3"));
    EXPECT_TRUE(parsed.errors.empty());
    ASSERT_EQ(parsed.args.size(), 1U);
    EXPECT_EQ(parsed.args[0].option_id.id(), KINDS_OPT_JOINED_OR_SEPARATE);
    ASSERT_EQ(parsed.args[0].values.size(), 1U);
    EXPECT_EQ(parsed.args[0].values[0], "3");

    parsed = parse_all(table, split2vec("-x4 tail"));
    EXPECT_TRUE(parsed.errors.empty());
    ASSERT_EQ(parsed.args.size(), 1U);
    EXPECT_EQ(parsed.args[0].option_id.id(), KINDS_OPT_JOINED_AND_SEPARATE);
    ASSERT_EQ(parsed.args[0].values.size(), 2U);
    EXPECT_EQ(parsed.args[0].values[0], "4");
    EXPECT_EQ(parsed.args[0].values[1], "tail");

    parsed = parse_all(table, split2vec("--rest one two"));
    EXPECT_TRUE(parsed.errors.empty());
    ASSERT_EQ(parsed.args.size(), 1U);
    EXPECT_EQ(parsed.args[0].option_id.id(), KINDS_OPT_REMAINING);
    ASSERT_EQ(parsed.args[0].values.size(), 2U);
    EXPECT_EQ(parsed.args[0].values[0], "one");
    EXPECT_EQ(parsed.args[0].values[1], "two");

    parsed = parse_all(table, split2vec("--tailz one two"));
    EXPECT_TRUE(parsed.errors.empty());
    ASSERT_EQ(parsed.args.size(), 1U);
    EXPECT_EQ(parsed.args[0].option_id.id(), KINDS_OPT_REMAINING_JOINED);
    ASSERT_EQ(parsed.args[0].values.size(), 3U);
    EXPECT_EQ(parsed.args[0].values[0], "z");
    EXPECT_EQ(parsed.args[0].values[1], "one");
    EXPECT_EQ(parsed.args[0].values[2], "two");
}

TEST_CASE(option_matches_and_render_style) {
    auto table = make_match_opt_table();

    const auto member = table.option(MATCH_OPT_MEMBER);
    const auto alias = table.option(MATCH_OPT_ALIAS_MEMBER);
    EXPECT_TRUE(member.matches(MATCH_OPT_GROUP));
    EXPECT_TRUE(alias.matches(MATCH_OPT_GROUP));
    EXPECT_EQ(alias.unaliased_option().id(), MATCH_OPT_MEMBER);

    EXPECT_EQ(table.option(MATCH_OPT_JOINED).render_style(), opt::Option::RenderJoinedStyle);
    EXPECT_EQ(table.option(MATCH_OPT_MEMBER).render_style(), opt::Option::RenderSeparateStyle);
    EXPECT_EQ(table.option(MATCH_OPT_OVERRIDE_FLAG).render_style(), opt::Option::RenderJoinedStyle);
}

TEST_CASE(flag_aliases_preserve_unaliased_addition_values) {
    auto table = make_alias_opt_table();
    auto parsed = parse_all(table, split2vec("--trap-defaults --emit-llvm"));

    EXPECT_TRUE(parsed.errors.empty());
    ASSERT_EQ(parsed.args.size(), 2U);

    EXPECT_EQ(parsed.args[0].option_id.id(), ALIAS_OPT_TRAP_DEFAULTS);
    EXPECT_EQ(parsed.args[0].unaliased_opt().id(), ALIAS_OPT_TRAP_EQ);
    ASSERT_TRUE(parsed.args[0].unaliased_addition_values.has_value());
    ASSERT_EQ(parsed.args[0].unaliased_addition_values->size(), 2U);
    EXPECT_EQ((*parsed.args[0].unaliased_addition_values)[0], "all");
    EXPECT_EQ((*parsed.args[0].unaliased_addition_values)[1], "undefined");
    {
        const auto trap_values = parsed.args[0].unaliased_values();
        ASSERT_EQ(trap_values.size(), 2U);
        EXPECT_EQ(trap_values[0], "all");
        EXPECT_EQ(trap_values[1], "undefined");
    }

    EXPECT_EQ(parsed.args[1].option_id.id(), ALIAS_OPT_EMIT_LLVM);
    EXPECT_EQ(parsed.args[1].unaliased_opt().id(), ALIAS_OPT_EMIT_EQ);
    ASSERT_TRUE(parsed.args[1].unaliased_addition_values.has_value());
    ASSERT_EQ(parsed.args[1].unaliased_addition_values->size(), 1U);
    EXPECT_EQ((*parsed.args[1].unaliased_addition_values)[0], "");
    {
        const auto emit_values = parsed.args[1].unaliased_values();
        ASSERT_EQ(emit_values.size(), 1U);
        EXPECT_EQ(emit_values[0], "");
    }
}

};  // TEST_SUITE(option_extended_coverage)

}  // namespace
