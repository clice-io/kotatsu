// Structs inside the options: groups of options, optional groups given all or nothing, and
// subcommands, a variant of structs the first word picks.

#include <expected>
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
    KANNOTATE(&host, cli::required = true)
    std::string host;
    int port = 0;
};

struct Index {
    Project where;
    std::optional<Remote> remote;
};

KANNOTATE_TYPE(struct Serve, description = "Start the server");

struct Serve {
    KANNOTATE(&help, cli::help)
    bool help = false;
    int port = 0;
};

struct ProjectFiles {
    KANNOTATE(&path, cli::positional = true)
    std::string path;
};

struct Tool {
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
    KANNOTATE(&config, cli::required = true)
    std::string config;
    std::variant<Serve> command;
};

struct Service {
    KANNOTATE(&command, tagged = true, tag_names = {"up", "index"})
    std::variant<Serve, Index> command;
};

template <typename T>
std::expected<T, Error> parse_line(std::string_view line) {
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
    ZASSERT(!index.has_value());
    ZEXPECT(index.error().kind == Error::Kind::MissingRequired);
    ZEXPECT(index.error().message == "required option --host <value> is missing");
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
    ZASSERT(!tool.has_value());
    ZEXPECT(tool.error().kind == Error::Kind::UnknownOption);
    ZEXPECT(tool.error().begin == 1U);
}

ZEST_CASE(missing_subcommand_fails) {
    auto tool = parse_line<Tool>("--verbose");
    ZASSERT(!tool.has_value());
    ZEXPECT(tool.error().kind == Error::Kind::MissingSubcommand);
    ZEXPECT(tool.error().message == "subcommand is required");
}

ZEST_CASE(unknown_subcommand_fails) {
    auto tool = parse_line<Tool>("bogus");
    ZASSERT(!tool.has_value());
    ZEXPECT(tool.error().kind == Error::Kind::UnknownSubcommand);
    ZEXPECT(tool.error().message == "unknown subcommand 'bogus'");
    ZEXPECT(tool.error().render() == "at argv[0]:\n  bogus\n  ^~~~~\n  unknown subcommand 'bogus'");
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
    ZASSERT(!query.has_value());
    ZEXPECT(query.error().kind == Error::Kind::UnknownSubcommand);
}

ZEST_CASE(help_of_a_subcommand_stops_it) {
    auto tool = parse_line<Tool>("serve --help --bogus");
    ZASSERT(tool.has_value());
    ZASSERT(tool->command.index() == 0U);
    ZEXPECT(std::get<Serve>(tool->command).help);
}

ZEST_CASE(error_before_help_fails) {
    auto tool = parse_line<Tool>("serve --bogus --help");
    ZASSERT(!tool.has_value());
    ZEXPECT(tool.error().message == "unknown option '--bogus'");
}

ZEST_CASE(subcommand_s_help_comes_before_what_the_command_requires) {
    auto daemon = parse_line<Daemon>("serve --help");
    ZASSERT(daemon.has_value());
    ZASSERT(daemon->command.index() == 0U);
    ZEXPECT(std::get<Serve>(daemon->command).help);
}

ZEST_CASE(command_s_required_option_left_out_fails_at_the_subcommand) {
    auto daemon = parse_line<Daemon>("serve --port 1");
    ZASSERT(!daemon.has_value());
    ZEXPECT(daemon.error().kind == Error::Kind::MissingRequired);
    ZEXPECT(daemon.error().begin == 0U);
    ZEXPECT(daemon.error().subcommands.empty());
}

ZEST_CASE(error_in_a_subcommand_names_it) {
    auto tool = parse_line<Tool>("--verbose serve --port many");
    ZASSERT(!tool.has_value());
    ZEXPECT(tool.error().kind == Error::Kind::InvalidValue);
    ZEXPECT(tool.error().subcommands == (std::vector<std::string>{"serve"}));
}

ZEST_CASE(tag_names_name_the_subcommands) {
    auto service = parse_line<Service>("up --port 2");
    ZASSERT(service.has_value());
    ZASSERT(service->command.index() == 0U);
    ZEXPECT(std::get<Serve>(service->command).port == 2);
}

ZEST_CASE(type_name_of_a_tagged_subcommand_fails) {
    auto service = parse_line<Service>("serve");
    ZASSERT(!service.has_value());
    ZEXPECT(service.error().kind == Error::Kind::UnknownSubcommand);
}

};  // ZEST_SUITE(cli_parse_struct)

}  // namespace

}  // namespace kotatsu::cli
