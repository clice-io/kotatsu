// The help every command takes without declaring it: -h and --help end the parse and ask for
// the usage of the command they are given to, named by the subcommands before them.

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "cli/harness/argv.h"
#include "kotatsu/cli/cli.h"
#include "kotatsu/zest/zest.h"

namespace kotatsu::cli {

namespace {

struct Stats {
    int limit = 10;
};

struct Index {
    std::optional<std::variant<Stats>> operation;
};

struct Serve {
    int port = 0;
};

struct Tool {
    KANNOTATE(&config, presence = required)
    std::string config;
    KANNOTATE(&path, cli::positional = true)
    std::string path;
    std::variant<Serve, Index> command;
};

struct Top {
    using signature = int();
};

struct Daemon {
    KANNOTATE(&jobs, presence = required)
    int jobs = 0;
    cli::Commands<Top> command;
};

struct Start {
    KANNOTATE(&name, cli::positional = true)
    std::string name;
};

struct Group {
    std::variant<Index> command;
};

int start(Start&) {
    return 0;
}

int group(Group&) {
    return 0;
}

[[maybe_unused]] const bool start_added = cli::add_command<Top>(start);
[[maybe_unused]] const bool group_added = cli::add_command<Top>(group);

struct Run {
    int jobs = 0;
    KANNOTATE(&command, cli::rest = true)
    std::vector<std::string> command;
};

/// A command naming -h an option of its own, which the help yields to.
struct Remote {
    KANNOTATE(&host, cli::names = {"-h", "--host"})
    std::string host;
};

/// A command naming both of the help's names itself, which so takes no help.
struct OwnHelp {
    KANNOTATE(&help, cli::names = {"-h", "--help"})
    bool help = false;
};

template <typename T>
Parsed<T> parse_line(std::string_view line) {
    return cli::parse<T>(line.empty() ? std::vector<std::string>{} : test::split(line));
}

ZEST_SUITE(cli_parse_help) {

ZEST_CASE(help_asks_for_the_program_s_usage) {
    auto tool = parse_line<Tool>("--help");
    ZEXPECT(!tool.has_value());
    ZEXPECT(tool.error() == nullptr);
    ZASSERT(tool.help() != nullptr);
    ZEXPECT(tool.help()->subcommands.empty());
}

ZEST_CASE(short_name_asks_too) {
    auto tool = parse_line<Tool>("-h");
    ZASSERT(tool.help() != nullptr);
    ZEXPECT(tool.help()->subcommands.empty());
}

ZEST_CASE(help_ends_the_parse_before_what_follows_and_what_is_required) {
    auto tool = parse_line<Tool>("-h --bogus serve");
    ZASSERT(tool.help() != nullptr);
    ZEXPECT(tool.help()->subcommands.empty());
}

ZEST_CASE(error_before_help_fails) {
    auto tool = parse_line<Tool>("--bogus --help");
    ZASSERT(tool.error() != nullptr);
    ZEXPECT(tool.error()->kind == Error::Kind::UnknownOption);
    ZEXPECT(tool.help() == nullptr);
}

ZEST_CASE(help_of_a_subcommand_names_it) {
    auto tool = parse_line<Tool>("--config c a.cc serve --help --bogus");
    ZASSERT(tool.help() != nullptr);
    ZEXPECT(tool.help()->subcommands == (std::vector<std::string>{"serve"}));
}

ZEST_CASE(help_of_a_subcommand_comes_before_what_the_command_requires) {
    auto tool = parse_line<Tool>("serve -h");
    ZASSERT(tool.help() != nullptr);
    ZEXPECT(tool.help()->subcommands == (std::vector<std::string>{"serve"}));
}

ZEST_CASE(help_of_a_nested_subcommand_names_both) {
    auto tool = parse_line<Tool>("--config c a.cc index stats --help");
    ZASSERT(tool.help() != nullptr);
    ZEXPECT(tool.help()->subcommands == (std::vector<std::string>{"index", "stats"}));
}

ZEST_CASE(help_between_subcommands_names_those_before_it) {
    auto tool = parse_line<Tool>("--config c a.cc index --help stats");
    ZASSERT(tool.help() != nullptr);
    ZEXPECT(tool.help()->subcommands == (std::vector<std::string>{"index"}));
}

ZEST_CASE(help_of_an_added_subcommand_names_it) {
    auto daemon = parse_line<Daemon>("start --help");
    ZASSERT(daemon.help() != nullptr);
    ZEXPECT(daemon.help()->subcommands == (std::vector<std::string>{"start"}));
}

ZEST_CASE(help_of_a_subcommand_under_an_added_one_names_all) {
    auto daemon = parse_line<Daemon>("--jobs 2 group index stats -h");
    ZASSERT(daemon.help() != nullptr);
    ZEXPECT(daemon.help()->subcommands == (std::vector<std::string>{"group", "index", "stats"}));
}

ZEST_CASE(help_after_the_dash_dash_is_an_argument) {
    auto run = parse_line<Run>("--jobs 2 -- --help");
    ZASSERT(run.has_value());
    ZEXPECT(run->command == (std::vector<std::string>{"--help"}));
}

ZEST_CASE(help_is_suggested_for_a_name_near_it) {
    auto run = parse_line<Run>("--hepl");
    ZASSERT(run.error() != nullptr);
    ZEXPECT(run.error()->suggestion == "--help");
}

ZEST_CASE(parse_into_on_help_leaves_the_options) {
    Run run{.jobs = 1, .command = {}};
    auto read = cli::parse_into(run, test::split("--jobs 3 --help"));
    ZASSERT(read.help() != nullptr);
    ZEXPECT(read.help()->subcommands.empty());
    ZEXPECT(run.jobs == 1);
}

ZEST_CASE(parse_into_without_help_or_error_has_a_value) {
    Run run;
    auto read = cli::parse_into(run, test::split("--jobs 3"));
    ZEXPECT(read.has_value());
    ZEXPECT(read.help() == nullptr);
    ZEXPECT(read.error() == nullptr);
    ZEXPECT(run.jobs == 3);
}

ZEST_CASE(option_named_h_keeps_its_name) {
    auto remote = parse_line<Remote>("-h example.com");
    ZASSERT(remote.has_value());
    ZEXPECT(remote->host == "example.com");
    ZEXPECT(parse_line<Remote>("--help").help() != nullptr);
}

ZEST_CASE(help_yielding_a_name_lists_the_other) {
    const auto entries = cli::usage_entries<Remote>();
    ZASSERT(entries.size() == 2U);
    ZEXPECT(entries[0].usage == "--help");
    ZEXPECT(entries[1].usage == "-h|--host <value>");
}

ZEST_CASE(command_naming_both_takes_no_help) {
    auto own = parse_line<OwnHelp>("--help");
    ZASSERT(own.has_value());
    ZEXPECT(own->help);
    const auto entries = cli::usage_entries<OwnHelp>();
    ZASSERT(entries.size() == 1U);
    ZEXPECT(entries[0].usage == "-h|--help");
    ZEXPECT(entries[0].help.empty());
}

};  // ZEST_SUITE(cli_parse_help)

}  // namespace

}  // namespace kotatsu::cli
