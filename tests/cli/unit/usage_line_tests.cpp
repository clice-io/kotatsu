// The line a usage says calls its command: the program, the subcommands' words, the positional
// arguments and the required options, [OPTIONS], the rest and the subcommand's placeholder; at
// every level a program reaches, with that level's defaults. And clice's commands, whose lines
// are those its users know.

#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "kotatsu/cli/cli.h"
#include "kotatsu/zest/zest.h"

namespace kotatsu::cli {

namespace {

struct Positionals {
    KANNOTATE(&path, cli::positional = true, cli::meta_var = "<PATH>")
    std::string path;
    KANNOTATE(&module, cli::positional = true, cli::meta_var = "<MODULE>")
    std::optional<std::string> module;
    KANNOTATE(&pair,
              presence = required,
              cli::positional = true,
              cli::meta_var = "<FROM> <TO>",
              cli::arg_num = 2)
    std::vector<std::string> pair;
    KANNOTATE(&files, cli::positional = true, cli::meta_var = "<FILE>")
    std::vector<std::string> files;
};

struct Inputs {
    KANNOTATE(&inputs, presence = required, cli::positional = true)
    std::vector<std::string> inputs;
};

struct Rest {
    KANNOTATE(&command, cli::rest = true, cli::meta_var = "<COMMAND>")
    std::vector<std::string> command;
};

struct RequiredRest {
    KANNOTATE(&command, presence = required, cli::rest = true, cli::meta_var = "<ARG>")
    std::vector<std::string> command;
};

struct Output {
    KANNOTATE(&path, cli::names = {"-o", "--output"}, cli::meta_var = "<FILE>", presence = required)
    std::string path;
};

struct Remote {
    KANNOTATE(&host, presence = required)
    std::string host;
};

struct Build {
    KANNOTATE(&target, cli::positional = true, cli::meta_var = "<TARGET>")
    std::string target;
    Output output;
    std::optional<Remote> remote;
    KANNOTATE(&level, presence = required, cli::style = KVStyle::Joined)
    int level = 0;
    KANNOTATE(&trace, presence = required, cli::hidden = true)
    std::string trace;
};

struct ByLocator {
    KANNOTATE(&locator, cli::positional = true, cli::meta_var = "<LOCATOR>")
    std::string locator;
    KANNOTATE(&new_name, cli::positional = true, cli::meta_var = "<NEW-NAME>")
    std::string new_name;
};

struct BySpec {
    KANNOTATE(&spec, presence = required, cli::meta_var = "<FILE>")
    std::string spec;
    bool dry = false;
};

struct Rename {
    KANNOTATE(&target, cli::exclusive = true, presence = required)
    std::variant<ByLocator, BySpec> target;
};

struct MaybeRename {
    KANNOTATE(&target, cli::exclusive = true)
    std::optional<std::variant<ByLocator, BySpec>> target;
};

struct ByName {
    std::string name;
};

struct ByIndex {
    int index = 0;
};

struct Pick {
    KANNOTATE(&by, cli::exclusive = true)
    std::variant<ByName, ByIndex> by;
};

struct MustPick {
    KANNOTATE(&by, cli::exclusive = true, presence = required)
    std::variant<ByName, ByIndex> by;
};

/// An alternative whose first argument a usage leaves out.
struct ByLegacy {
    KANNOTATE(&legacy, cli::hidden = true)
    bool legacy = false;
    std::string name;
};

struct ById {
    std::string id;
};

struct Quiet {
    KANNOTATE(&by, cli::exclusive = true, presence = required)
    std::variant<ByLegacy, ById> by;
};

struct WithRest {
    KANNOTATE(&command, cli::rest = true)
    std::vector<std::string> command;
};

struct Pass {
    KANNOTATE(&how, cli::exclusive = true)
    std::variant<WithRest, BySpec> how;
};

struct Shapes {
    KANNOTATE(&size, presence = required)
    std::tuple<int, int> size;
    KANNOTATE(&tags, presence = required, cli::comma = true)
    std::vector<std::string> tags;
    KANNOTATE(&force, presence = required)
    bool force = false;
};

KANNOTATE_TYPE(struct Stats, description = "Index statistics");

struct Stats {
    int limit = 20;
};

KANNOTATE_TYPE(struct File, description = "One file");

struct File {
    KANNOTATE(&path, cli::positional = true, cli::meta_var = "<PATH>")
    std::string path;
    int depth = 3;
};

struct Index {
    int jobs = 1;
    KANNOTATE(&operation, cli::meta_var = "<operation>")
    std::optional<std::variant<Stats, File>> operation;
};

struct App {
    KANNOTATE(&verbose, cli::global = true)
    bool verbose = false;
    std::variant<Index, Stats> command;
};

struct Steps {
    using signature = int();
};

struct Daemon {
    KANNOTATE(&step, cli::meta_var = "<step>")
    cli::Commands<Steps> step;
};

struct Start {
    KANNOTATE(&name, cli::positional = true, cli::meta_var = "<NAME>")
    std::string name;
    int workers = 4;
};

int run_start(Start&) {
    return 0;
}

int run_index(Index&) {
    return 0;
}

[[maybe_unused]] const bool start_added = cli::add_command<Steps>(run_start);
[[maybe_unused]] const bool index_added = cli::add_command<Steps>(run_index);

/// The line `usage` says calls its command.
std::string line_of(const std::string& usage) {
    constexpr std::string_view prefix = "usage: ";
    const auto at = usage.find(prefix);
    ZASSERT(at != std::string::npos);
    const auto begin = at + prefix.size();
    return usage.substr(begin, usage.find('\n', begin) - begin);
}

namespace clice {

// clice's commands, what each takes without what it does.

struct Subcommands {
    using signature = int();
};

KANNOTATE_TYPE(struct Clice, description = "A C++ development toolkit built on LLVM/Clang");

struct Clice {
    KANNOTATE(&version,
              cli::names = {"-v", "--version"},
              cli::stop = true,
              description = "Print the version and exit")
    bool version = false;
    cli::Commands<Subcommands> command;
};

struct FileTarget {
    KANNOTATE(&path, cli::positional = true, cli::meta_var = "<PATH>")
    std::string path;
};

struct SymbolTarget {
    KANNOTATE(&locator, cli::positional = true, cli::meta_var = "<LOCATOR>")
    std::string locator;
};

struct ProjectOptions {
    std::optional<std::string> root;
};

struct LogLevelOption {
    std::string log_level = "info";
};

struct Serve {
    ProjectOptions where;
    LogLevelOption log;
};

struct ProjectFiles {
    std::string filter = "all";
    ProjectOptions where;
};

struct References {
    SymbolTarget target;
    bool include_declaration = false;
};

struct SymbolSearch {
    KANNOTATE(&locator, cli::positional = true, cli::meta_var = "<LOCATOR>")
    std::optional<std::string> locator;
    int limit = 100;
};

struct Query {
    KANNOTATE(&operation, cli::meta_var = "<question>")
    std::variant<ProjectFiles, References, SymbolSearch> operation;
};

struct Format {
    KANNOTATE(&paths, cli::positional = true, cli::meta_var = "<PATH>")
    std::vector<std::string> paths;
    bool check = false;
};

struct Stats {
    int limit = 10;
};

struct File {
    FileTarget target;
    ProjectOptions where;
};

struct Index {
    ProjectOptions where;
    KANNOTATE(&operation, cli::meta_var = "<operation>")
    std::optional<std::variant<Stats, File>> operation;
};

struct Edge {
    KANNOTATE(&modules,
              presence = required,
              cli::positional = true,
              cli::meta_var = "<FROM> <TO>",
              cli::arg_num = 2)
    std::vector<std::string> modules;
};

struct Interface {
    KANNOTATE(&module, cli::positional = true, cli::meta_var = "<MODULE>")
    std::optional<std::string> module;
};

struct Modules {
    int limit = 20;
    KANNOTATE(&operation, cli::meta_var = "<view>")
    std::optional<std::variant<Edge, Interface>> operation;
};

struct Analyze {
    KANNOTATE(&operation, cli::meta_var = "<analysis>")
    std::variant<Modules> operation;
};

struct Convert {
    KANNOTATE(&database, cli::positional = true, cli::meta_var = "<DATABASE>")
    std::optional<std::string> database;
    bool json = false;
    KANNOTATE(&command, cli::rest = true, cli::meta_var = "<COMMAND>")
    std::vector<std::string> command;
};

struct Command {
    KANNOTATE(&path, cli::positional = true, cli::meta_var = "<PATH>")
    std::optional<std::string> path;
    KANNOTATE(&operation, cli::meta_var = "<operation>")
    std::optional<std::variant<Convert>> operation;
};

struct ByLocator {
    KANNOTATE(&inputs,
              presence = required,
              cli::positional = true,
              cli::meta_var = "<LOCATOR> <NEW-NAME>",
              cli::arg_num = 2)
    std::vector<std::string> inputs;
};

struct BySpec {
    KANNOTATE(&spec, presence = required, cli::meta_var = "<FILE>")
    std::string spec;
};

struct Rename {
    KANNOTATE(&target, cli::exclusive = true, presence = required)
    std::variant<ByLocator, BySpec> target;
    bool dry_run = false;
};

struct Refactor {
    KANNOTATE(&operation, cli::meta_var = "<refactoring>")
    std::variant<Rename> operation;
};

template <typename T>
int run(T&) {
    return 0;
}

[[maybe_unused]] const bool added = [] {
    cli::add_command<Subcommands>(run<Serve>);
    cli::add_command<Subcommands>(run<Query>);
    cli::add_command<Subcommands>(run<Refactor>);
    cli::add_command<Subcommands>(run<Index>);
    cli::add_command<Subcommands>(run<Format>);
    cli::add_command<Subcommands>(run<Command>);
    cli::add_command<Subcommands>(run<Analyze>);
    return true;
}();

/// The line clice's usage says calls the command `words` name.
std::string line(std::vector<std::string> words) {
    return line_of(cli::usage<Clice>("clice", words));
}

}  // namespace clice

ZEST_SUITE(cli_usage_line) {

ZEST_CASE(positional_arguments_are_bracketed_unless_required) {
    ZEXPECT(line_of(cli::usage<Positionals>("tool")) ==
            "tool <PATH> [<MODULE>] <FROM> <TO> [<FILE>...] [OPTIONS]");
}

ZEST_CASE(required_list_is_not_bracketed) {
    ZEXPECT(line_of(cli::usage<Inputs>("tool")) == "tool <value>... [OPTIONS]");
}

ZEST_CASE(rest_follows_the_options) {
    ZEXPECT(line_of(cli::usage<Rest>("tool")) == "tool [OPTIONS] [-- <COMMAND>...]");
    ZEXPECT(line_of(cli::usage<RequiredRest>("tool")) == "tool [OPTIONS] -- <ARG>...");
}

ZEST_CASE(required_options_are_written_by_their_first_name) {
    ZEXPECT(line_of(cli::usage<Build>("tool")) ==
            "tool <TARGET> -o <FILE> --level=<value> [OPTIONS]");
}

ZEST_CASE(required_exclusive_field_is_its_alternatives) {
    ZEXPECT(line_of(cli::usage<Rename>("tool")) ==
            "tool (<LOCATOR> <NEW-NAME> | --spec <FILE>) [OPTIONS]");
}

ZEST_CASE(optional_exclusive_field_with_positional_arguments_is_bracketed) {
    ZEXPECT(line_of(cli::usage<MaybeRename>("tool")) ==
            "tool [<LOCATOR> <NEW-NAME> | --spec <FILE>] [OPTIONS]");
}

ZEST_CASE(optional_exclusive_field_of_options_is_left_to_them) {
    ZEXPECT(line_of(cli::usage<Pick>("tool")) == "tool [OPTIONS]");
}

ZEST_CASE(alternative_requiring_nothing_is_its_first_shown_argument) {
    ZEXPECT(line_of(cli::usage<Quiet>("tool")) == "tool (--name <value> | --id <value>) [OPTIONS]");
}

ZEST_CASE(alternative_taking_the_rest_is_written_where_the_field_is) {
    ZEXPECT(line_of(cli::usage<Pass>("tool")) == "tool [-- <value>... | --spec <FILE>] [OPTIONS]");
}

ZEST_CASE(required_options_of_every_shape_are_written) {
    ZEXPECT(line_of(cli::usage<Shapes>("tool")) ==
            "tool --size <value1> <value2> --tags,<value>[,<value>...] --force [OPTIONS]");
}

ZEST_CASE(required_exclusive_field_of_options_is_their_first_ones) {
    ZEXPECT(line_of(cli::usage<MustPick>("tool")) ==
            "tool (--name <value> | --index <value>) [OPTIONS]");
}

ZEST_CASE(program_s_own_usage_ends_with_its_subcommand) {
    ZEXPECT(line_of(cli::usage<App>("app")) == "app [OPTIONS] <command>");
}

ZEST_CASE(subcommand_s_usage_follows_its_word) {
    const auto usage = cli::usage<App>("app", {"index"});
    ZEXPECT(line_of(usage) == "app index [OPTIONS] [<operation>]");
    ZEXPECT(usage.contains("--jobs <value>                  no description provided [default: 1]"));
    ZEXPECT(usage.contains("  stats  Index statistics\n  file   One file\n"));
}

ZEST_CASE(nested_subcommand_s_usage_follows_both_words) {
    const auto usage = cli::usage<App>("app", {"index", "file"});
    ZEXPECT(usage.starts_with("One file\n\nusage: app index file <PATH> [OPTIONS]\n"));
    ZEXPECT(usage.contains("[default: 3]"));
}

ZEST_CASE(subcommand_after_the_first_alternative_says_its_defaults) {
    const auto usage = cli::usage<App>("app", {"stats"});
    ZEXPECT(line_of(usage) == "app stats [OPTIONS]");
    ZEXPECT(usage.contains("[default: 20]"));
}

ZEST_CASE(added_subcommand_s_usage_follows_its_word) {
    ZEXPECT(line_of(cli::usage<Daemon>("daemon")) == "daemon [OPTIONS] [<step>]");
    const auto usage = cli::usage<Daemon>("daemon", {"start"});
    ZEXPECT(line_of(usage) == "daemon start <NAME> [OPTIONS]");
    ZEXPECT(usage.contains("[default: 4]"));
}

ZEST_CASE(subcommand_under_an_added_one_follows_both_words) {
    ZEXPECT(line_of(cli::usage<Daemon>("daemon", {"index", "file"})) ==
            "daemon index file <PATH> [OPTIONS]");
}

ZEST_CASE(word_naming_no_subcommand_ends_the_program, crashes = true) {
    static_cast<void>(cli::usage<App>("app", {"index", "bogus"}));
}

ZEST_CASE(word_past_a_command_without_subcommands_ends_the_program, crashes = true) {
    static_cast<void>(cli::usage<App>("app", {"stats", "more"}));
}

ZEST_CASE(lines_are_those_clice_wrote) {
    const std::vector<std::pair<std::vector<std::string>, std::string>> lines = {
        {{"serve"},                           "clice serve [OPTIONS]"                                         },
        {{"query", "references"},             "clice query references <LOCATOR> [OPTIONS]"                    },
        {{"query", "symbol-search"},          "clice query symbol-search [<LOCATOR>] [OPTIONS]"               },
        {{"query", "project-files"},          "clice query project-files [OPTIONS]"                           },
        {{"format"},                          "clice format [<PATH>...] [OPTIONS]"                            },
        {{"index"},                           "clice index [OPTIONS] [<operation>]"                           },
        {{"index", "stats"},                  "clice index stats [OPTIONS]"                                   },
        {{"index", "file"},                   "clice index file <PATH> [OPTIONS]"                             },
        {{"analyze", "modules"},              "clice analyze modules [OPTIONS] [<view>]"                      },
        {{"analyze", "modules", "edge"},      "clice analyze modules edge <FROM> <TO> [OPTIONS]"              },
        {{"analyze", "modules", "interface"},
         "clice analyze modules interface [<MODULE>] [OPTIONS]"                                               },
        {{"command", "convert"},              "clice command convert [<DATABASE>] [OPTIONS] [-- <COMMAND>...]"},
        {{"refactor", "rename"},
         "clice refactor rename (<LOCATOR> <NEW-NAME> | --spec <FILE>) [OPTIONS]"                             },
    };
    for(const auto& [words, expected]: lines) {
        ZEST_CONTEXT("{}", expected);
        ZEXPECT(clice::line(words) == expected);
    }
}

ZEST_CASE(lines_clice_wrote_otherwise_are_the_rule_s) {
    ZEXPECT(clice::line({}) == "clice [OPTIONS] [<command>]");
    ZEXPECT(clice::line({"query"}) == "clice query [OPTIONS] <question>");
    ZEXPECT(clice::line({"analyze"}) == "clice analyze [OPTIONS] <analysis>");
    ZEXPECT(clice::line({"refactor"}) == "clice refactor [OPTIONS] <refactoring>");
    ZEXPECT(clice::line({"command"}) == "clice command [<PATH>] [OPTIONS] [<operation>]");
}

ZEST_CASE(clice_s_commands_are_listed_by_name) {
    const auto usage = cli::usage<clice::Clice>("clice");
    ZEXPECT(
        usage.contains("  -h|--help                       display this help and exit\n"
                       "  -v|--version                    Print the version and exit\n"));
    ZEXPECT(
        usage.contains("Subcommands:\n"
                       "  analyze\n"
                       "  command\n"
                       "  format\n"
                       "  index\n"
                       "  query\n"
                       "  refactor\n"
                       "  serve\n"));
}

};  // ZEST_SUITE(cli_usage_line)

}  // namespace

}  // namespace kotatsu::cli
