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
    KOTATSU_ANNOTATE(cli::names = {"-j", "--jobs"})
    <unsigned> jobs = 0;
};

struct Remote {
    KOTATSU_ANNOTATE(cli::required = true)
    <std::string> host;
    int port = 0;
};

struct Index {
    Project where;
    std::optional<Remote> remote;
};

struct Serve {
    constexpr static std::string_view description = "Start the server";
    cli::Help help;
    int port = 0;
};

struct ProjectFiles {
    KOTATSU_ANNOTATE(cli::positional = true)
    <std::string> path;
};

struct Tool {
    bool verbose = false;
    std::variant<Serve, ProjectFiles> command;
};

struct Main {
    KOTATSU_ANNOTATE(cli::positional = true)
    <std::optional<std::string>> path;
    std::optional<std::variant<Serve>> operation;
};

template <typename T>
std::expected<T, Error> parse_line(std::string_view line) {
    return cli::parse<T>(line.empty() ? std::vector<std::string>{} : test::split(line));
}

ZEST_SUITE(cli_parse_struct) {

ZEST_CASE(group_options_are_named_like_any_other) {
    auto index = parse_line<Index>("--workspace w -j 2");
    ZASSERT(index.has_value());
    ZEXPECT(*index->where.workspace == "w");
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

ZEST_CASE(optional_group_given_needs_its_required_options) {
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

ZEST_CASE(options_after_the_subcommand_are_its_own) {
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
    ZEXPECT(*file->path == "a.cc");
    ZEXPECT(!file->operation.has_value());
    auto serve = parse_line<Main>("serve --port 1");
    ZASSERT(serve.has_value());
    ZASSERT(serve->operation.has_value());
    ZEXPECT(std::get<Serve>(*serve->operation).port == 1);
}

ZEST_CASE(help_of_a_subcommand_stops_it) {
    auto tool = parse_line<Tool>("serve --help --bogus");
    ZASSERT(tool.has_value());
    ZEXPECT(std::get<Serve>(tool->command).help);
}

ZEST_CASE(error_before_help_fails) {
    auto tool = parse_line<Tool>("serve --bogus --help");
    ZASSERT(!tool.has_value());
    ZEXPECT(tool.error().message == "unknown option '--bogus'");
}

};  // ZEST_SUITE(cli_parse_struct)

}  // namespace

}  // namespace kotatsu::cli
