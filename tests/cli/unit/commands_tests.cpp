// Subcommands files add to a set, which a cli::Commands field takes without naming them.

#include <string>
#include <vector>

#include "kotatsu/cli/cli.h"
#include "kotatsu/zest/zest.h"

namespace kotatsu::cli {

namespace {

struct Top {
    using signature = int(int);
};

struct Tool {
    bool verbose = false;
    cli::Commands<Top> command;
};

struct Build {
    constexpr static std::string_view description = "Build the project";
    int jobs = 1;
};

struct ServeIt {
    constexpr static std::string_view description = "Serve the project";
    int port = 0;
};

struct Internal {
    bool quiet = false;
};

int run_build(Build& build, int base) {
    return base + build.jobs;
}

int run_serve(ServeIt& serve, int base) {
    return base + serve.port;
}

int run_internal(Internal&, int) {
    return -1;
}

[[maybe_unused]] const bool build_added = cli::add_command<Top>(run_build);
[[maybe_unused]] const bool serve_added = cli::add_command<Top>(run_serve, {.name = "serve"});
[[maybe_unused]] const bool internal_added =
    cli::add_command<Top>(run_internal, {.name = "internal", .hidden = true});

ZEST_SUITE(cli_commands) {

ZEST_CASE(word_picks_an_added_subcommand) {
    auto tool = cli::parse<Tool>(std::vector<std::string>{"--verbose", "build", "--jobs", "3"});
    ZASSERT(tool.has_value());
    ZASSERT(static_cast<bool>(tool->command));
    ZEXPECT(tool->command.name() == "build");
    ZASSERT(tool->command.get_if<Build>() != nullptr);
    ZEXPECT(tool->command.get_if<Build>()->jobs == 3);
    ZEXPECT(tool->command.get_if<ServeIt>() == nullptr);
}

ZEST_CASE(calling_runs_its_callback) {
    auto tool = cli::parse<Tool>(std::vector<std::string>{"serve", "--port", "8"});
    ZASSERT(tool.has_value());
    ZEXPECT(tool->command(100) == 108);
}

ZEST_CASE(no_word_leaves_it_empty) {
    auto tool = cli::parse<Tool>(std::vector<std::string>{"--verbose"});
    ZASSERT(tool.has_value());
    ZEXPECT(!tool->command);
}

ZEST_CASE(unknown_word_fails_with_the_closest) {
    auto tool = cli::parse<Tool>(std::vector<std::string>{"buidl"});
    ZASSERT(!tool.has_value());
    ZEXPECT(tool.error().kind == Error::Kind::UnknownSubcommand);
    ZEXPECT(tool.error().suggestion == "build");
}

ZEST_CASE(usage_lists_the_shown_ones_by_name) {
    const auto entries = cli::usage_entries<Tool>();
    ZASSERT(entries.size() == 3U);
    ZEXPECT(entries[1].usage == "build");
    ZEXPECT(entries[1].help == "Build the project");
    ZEXPECT(entries[2].usage == "serve");
}

ZEST_CASE(hidden_one_still_parses) {
    auto tool = cli::parse<Tool>(std::vector<std::string>{"internal", "--quiet"});
    ZASSERT(tool.has_value());
    ZEXPECT(tool->command(0) == -1);
}

ZEST_CASE(held_one_writes_back) {
    auto tool = cli::parse<Tool>(std::vector<std::string>{"build", "--jobs", "2"});
    ZASSERT(tool.has_value());
    ZEXPECT(cli::to_argv(*tool) == (std::vector<std::string>{"build", "--jobs=2"}));
}

};  // ZEST_SUITE(cli_commands)

}  // namespace

}  // namespace kotatsu::cli
