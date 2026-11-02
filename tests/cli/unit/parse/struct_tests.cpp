// Structs inside the options: groups of options, optional groups given all or nothing, and
// subcommands, a variant of structs the first word picks.

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "cli/harness/argv.h"
#include "kotatsu/cli/cli.h"
#include "kotatsu/zest/zest.h"

namespace kotatsu::cli {

namespace {

struct Project {
    std::optional<std::string> workspace;
    KANNOTATE(&jobs, cli::names = {"-j", "--jobs"})
    unsigned jobs = 0;
};

struct Remote {
    KANNOTATE(&host, presence = required)
    std::string host;
    int port = 0;
};

struct Index {
    Project where;
    std::optional<Remote> remote;
};

KANNOTATE_TYPE(struct Serve, description = "Start the server");

struct Serve {
    int port = 0;
    KANNOTATE(&version, cli::stop = true)
    bool version = false;
};

struct ProjectFiles {
    KANNOTATE(&path, cli::positional = true)
    std::string path;
};

struct Tool {
    KANNOTATE(&verbose, cli::global = true)
    bool verbose = false;
    std::variant<Serve, ProjectFiles> command;
};

struct Main {
    KANNOTATE(&path, cli::positional = true)
    std::optional<std::string> path;
    std::optional<std::variant<Serve>> operation;
};

/// Subcommands that may be left out, beside no positional argument.
struct Query {
    std::optional<std::variant<Serve>> operation;
};

struct Daemon {
    KANNOTATE(&config, presence = required, cli::global = true)
    std::string config;
    std::variant<Serve> command;
};

/// A command with a job of its own, beside subcommands that take none of its arguments.
struct Indexer {
    std::optional<std::string> workspace;
    KANNOTATE(&json, presence = required)
    bool json = false;
    std::optional<std::variant<Serve>> operation;
};

/// Options every subcommand shares, in a group.
struct Shared {
    int level = 0;
};

struct Grouped {
    KANNOTATE(&shared, cli::global = true)
    Shared shared;
    bool own = false;
    std::variant<Serve> command;
};

struct Service {
    KANNOTATE(&command, tagged = true, tag_names = {"up", "index"})
    std::variant<Serve, Index> command;
};

template <typename T>
Parsed<T> parse_line(std::string_view line) {
    return cli::parse<T>(line.empty() ? std::vector<std::string>{} : test::split(line));
}

ZEST_SUITE(cli_parse_struct) {

ZEST_CASE(group_options_are_named_like_any_other) {
    auto index = parse_line<Index>("--workspace w -j 2");
    ZASSERT(index.has_value());
    ZEXPECT(index->where.workspace == std::optional<std::string>("w"));
    ZEXPECT(index->where.jobs == 2U);
    ZEXPECT(!index->remote.has_value());
}

ZEST_CASE(optional_group_is_given_by_any_option_of_it) {
    auto index = parse_line<Index>("--host h --port 1");
    ZASSERT(index.has_value());
    ZASSERT(index->remote.has_value());
    ZEXPECT(std::string(index->remote->host) == "h");
    ZEXPECT(index->remote->port == 1);
}

ZEST_CASE(optional_group_without_its_required_options_fails) {
    auto index = parse_line<Index>("--port 1");
    ZASSERT(index.error() != nullptr);
    ZEXPECT(index.error()->kind == Error::Kind::MissingRequired);
    ZEXPECT(index.error()->message == "required option --host <value> is missing");
}

ZEST_CASE(subcommand_word_picks_the_alternative) {
    auto tool = parse_line<Tool>("--verbose serve --port 8");
    ZASSERT(tool.has_value());
    ZEXPECT(tool->verbose);
    ZASSERT(tool->command.index() == 0U);
    ZEXPECT(std::get<Serve>(tool->command).port == 8);
}

ZEST_CASE(subcommand_names_are_its_type_in_kebab_case) {
    auto tool = parse_line<Tool>("project-files a.cc");
    ZASSERT(tool.has_value());
    ZASSERT(tool->command.index() == 1U);
    ZEXPECT(std::string(std::get<ProjectFiles>(tool->command).path) == "a.cc");
}

ZEST_CASE(command_s_option_after_the_subcommand_fails) {
    auto tool = parse_line<Tool>("serve --verbose");
    ZASSERT(tool.error() != nullptr);
    ZEXPECT(tool.error()->kind == Error::Kind::UnknownOption);
    ZEXPECT(tool.error()->begin == 1U);
}

ZEST_CASE(command_s_option_before_a_subcommand_fails) {
    auto indexer = parse_line<Indexer>("--workspace /b serve");
    ZASSERT(indexer.error() != nullptr);
    ZEXPECT(indexer.error()->kind == Error::Kind::Conflict);
    ZEXPECT(indexer.error()->message ==
            "subcommand 'serve' cannot be used with --workspace <value>");
    // At the subcommand's word.
    ZEXPECT(indexer.error()->begin == 2U);
}

ZEST_CASE(command_s_flag_before_a_subcommand_fails) {
    auto indexer = parse_line<Indexer>("--json serve");
    ZASSERT(indexer.error() != nullptr);
    ZEXPECT(indexer.error()->kind == Error::Kind::Conflict);
    ZEXPECT(indexer.error()->message == "subcommand 'serve' cannot be used with --json");
}

ZEST_CASE(command_s_positional_before_a_subcommand_fails) {
    auto main = parse_line<Main>("a.cc serve");
    ZASSERT(main.error() != nullptr);
    ZEXPECT(main.error()->kind == Error::Kind::Conflict);
    ZEXPECT(main.error()->message == "subcommand 'serve' cannot be used with <value>");
}

ZEST_CASE(command_s_required_option_is_not_required_with_a_subcommand) {
    auto indexer = parse_line<Indexer>("serve --port 3");
    ZASSERT(indexer.has_value());
    ZASSERT(indexer->operation.has_value());
    ZEXPECT(std::get<Serve>(*indexer->operation).port == 3);
}

ZEST_CASE(command_s_option_without_a_subcommand_is_read) {
    auto indexer = parse_line<Indexer>("--workspace /b --json");
    ZASSERT(indexer.has_value());
    ZEXPECT(indexer->workspace == std::optional<std::string>("/b"));
    ZEXPECT(!indexer->operation.has_value());
}

ZEST_CASE(global_group_goes_with_a_subcommand) {
    auto grouped = parse_line<Grouped>("--level 2 serve");
    ZASSERT(grouped.has_value());
    ZEXPECT(grouped->shared.level == 2);
    auto own = parse_line<Grouped>("--own serve");
    ZASSERT(own.error() != nullptr);
    ZEXPECT(own.error()->kind == Error::Kind::Conflict);
}

ZEST_CASE(missing_subcommand_fails) {
    auto tool = parse_line<Tool>("--verbose");
    ZASSERT(tool.error() != nullptr);
    ZEXPECT(tool.error()->kind == Error::Kind::MissingSubcommand);
    ZEXPECT(tool.error()->message == "subcommand is required");
}

ZEST_CASE(unknown_subcommand_fails) {
    auto tool = parse_line<Tool>("bogus");
    ZASSERT(tool.error() != nullptr);
    ZEXPECT(tool.error()->kind == Error::Kind::UnknownSubcommand);
    ZEXPECT(tool.error()->message == "unknown subcommand 'bogus'");
    ZEXPECT(tool.error()->render() ==
            "at argv[0]:\n  bogus\n  ^~~~~\n  unknown subcommand 'bogus'");
}

ZEST_CASE(optional_subcommand_may_be_left_out) {
    auto file = parse_line<Main>("a.cc");
    ZASSERT(file.has_value());
    ZEXPECT(file->path == std::optional<std::string>("a.cc"));
    ZEXPECT(!file->operation.has_value());
}

ZEST_CASE(optional_subcommand_given_is_read) {
    auto serve = parse_line<Main>("serve --port 1");
    ZASSERT(serve.has_value());
    ZASSERT(serve->operation.has_value());
    ZEXPECT(std::get<Serve>(*serve->operation).port == 1);
}

ZEST_CASE(unknown_word_where_a_subcommand_may_go_fails) {
    auto query = parse_line<Query>("nope");
    ZASSERT(query.error() != nullptr);
    ZEXPECT(query.error()->kind == Error::Kind::UnknownSubcommand);
}

ZEST_CASE(subcommand_s_stop_option_skips_what_the_command_requires) {
    auto daemon = parse_line<Daemon>("serve --version --bogus");
    ZASSERT(daemon.has_value());
    ZASSERT(daemon->command.index() == 0U);
    ZEXPECT(std::get<Serve>(daemon->command).version);
}

ZEST_CASE(command_s_required_option_left_out_fails_at_the_subcommand) {
    auto daemon = parse_line<Daemon>("serve --port 1");
    ZASSERT(daemon.error() != nullptr);
    ZEXPECT(daemon.error()->kind == Error::Kind::MissingRequired);
    ZEXPECT(daemon.error()->begin == 0U);
    ZEXPECT(daemon.error()->subcommands.empty());
}

ZEST_CASE(error_in_a_subcommand_names_it) {
    auto tool = parse_line<Tool>("--verbose serve --port many");
    ZASSERT(tool.error() != nullptr);
    ZEXPECT(tool.error()->kind == Error::Kind::InvalidValue);
    ZEXPECT(tool.error()->subcommands == (std::vector<std::string>{"serve"}));
}

ZEST_CASE(tag_names_name_the_subcommands) {
    auto service = parse_line<Service>("up --port 2");
    ZASSERT(service.has_value());
    ZASSERT(service->command.index() == 0U);
    ZEXPECT(std::get<Serve>(service->command).port == 2);
}

ZEST_CASE(type_name_of_a_tagged_subcommand_fails) {
    auto service = parse_line<Service>("serve");
    ZASSERT(service.error() != nullptr);
    ZEXPECT(service.error()->kind == Error::Kind::UnknownSubcommand);
}

};  // ZEST_SUITE(cli_parse_struct)

}  // namespace

}  // namespace kotatsu::cli
