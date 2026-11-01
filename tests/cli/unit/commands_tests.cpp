// Subcommands files add to a set, which a cli::Commands field takes without naming them.

#include <string>
#include <utility>
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

KANNOTATE_TYPE(struct Build, description = "Build the project");

struct Build {
    int jobs = 1;
};

KANNOTATE_TYPE(struct ServeIt, description = "Serve the project");

struct ServeIt {
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

struct Lifecycle {
    using signature = int();
};

struct Service {
    std::string name;
};

struct Daemon {
    cli::Commands<Lifecycle> command;
};

int start(Service&) {
    return 1;
}

int stop(Service&) {
    return 2;
}

[[maybe_unused]] const bool start_added = cli::add_command<Lifecycle>(start, {.name = "start"});
[[maybe_unused]] const bool stop_added = cli::add_command<Lifecycle>(stop, {.name = "stop"});

/// A set a test adds to while it runs.
struct Late {
    using signature = int();
};

struct LateOptions {
    cli::Commands<Late> command;
};

/// A set one of whose subcommands holds the set again.
struct Loop {
    using signature = int();
};

struct Again {
    cli::Commands<Loop> command;
};

struct LoopOptions {
    cli::Commands<Loop> command;
};

int again(Again&) {
    return 0;
}

[[maybe_unused]] const bool again_added = cli::add_command<Loop>(again);

/// A cli::Commands field, an annotation itself, annotated again.
struct MustPick {
    KANNOTATE(command, cli::required = true)
    cli::Commands<Top> command;
};

ZEST_SUITE(cli_commands) {

ZEST_CASE(word_picks_an_added_subcommand) {
    auto tool = cli::parse<Tool>(std::vector<std::string>{"--verbose", "build", "--jobs", "3"});
    ZASSERT(tool.has_value());
    ZASSERT(static_cast<bool>(tool->command));
    ZEXPECT(tool->command.name() == "build");
    ZASSERT(tool->command.get_if<Build>() != nullptr);
    ZEXPECT(tool->command.get_if<Build>()->jobs == 3);
}

ZEST_CASE(struct_of_another_subcommand_is_not_held) {
    auto tool = cli::parse<Tool>(std::vector<std::string>{"build"});
    ZASSERT(tool.has_value());
    ZEXPECT(tool->command.get_if<ServeIt>() == nullptr);
}

ZEST_CASE(calling_runs_its_callback) {
    auto tool = cli::parse<Tool>(std::vector<std::string>{"serve", "--port", "8"});
    ZASSERT(tool.has_value());
    ZASSERT(static_cast<bool>(tool->command));
    ZEXPECT(tool->command(100) == 108);
}

ZEST_CASE(no_word_leaves_it_empty) {
    auto tool = cli::parse<Tool>(std::vector<std::string>{"--verbose"});
    ZASSERT(tool.has_value());
    ZEXPECT(!tool->command);
}

ZEST_CASE(one_struct_added_twice_runs_each_callback) {
    auto daemon = cli::parse<Daemon>(std::vector<std::string>{"stop"});
    ZASSERT(daemon.has_value());
    ZASSERT(static_cast<bool>(daemon->command));
    ZEXPECT(daemon->command() == 2);
}

ZEST_CASE(moved_from_holds_nothing) {
    auto tool = cli::parse<Tool>(std::vector<std::string>{"build"});
    ZASSERT(tool.has_value());
    auto moved = std::move(tool->command);
    ZEXPECT(static_cast<bool>(moved));
    ZEXPECT(!tool->command);
    ZEXPECT(tool->command.get_if<Build>() == nullptr);
}

ZEST_CASE(error_in_a_subcommand_names_it) {
    auto tool = cli::parse<Tool>(std::vector<std::string>{"build", "--jobs", "x"});
    ZASSERT(!tool.has_value());
    ZEXPECT(tool.error().subcommands == (std::vector<std::string>{"build"}));
}

ZEST_CASE(unknown_word_near_a_hidden_one_suggests_none) {
    auto tool = cli::parse<Tool>(std::vector<std::string>{"internl"});
    ZASSERT(!tool.has_value());
    ZEXPECT(tool.error().suggestion.empty());
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
    ZASSERT(static_cast<bool>(tool->command));
    ZEXPECT(tool->command(0) == -1);
}

ZEST_CASE(held_one_writes_back) {
    auto tool = cli::parse<Tool>(std::vector<std::string>{"build", "--jobs", "2"});
    ZASSERT(tool.has_value());
    ZEXPECT(cli::to_argv(*tool) == (std::vector<std::string>{"build", "--jobs=2"}));
}

ZEST_CASE(hidden_one_writes_back) {
    auto tool = cli::parse<Tool>(std::vector<std::string>{"internal", "--quiet"});
    ZASSERT(tool.has_value());
    ZEXPECT(cli::to_argv(*tool) == (std::vector<std::string>{"internal", "--quiet"}));
}

ZEST_CASE(none_held_writes_nothing) {
    ZEXPECT(cli::to_argv(Tool{}).empty());
}

ZEST_CASE(annotated_field_keeps_its_subcommands) {
    auto picked = cli::parse<MustPick>(std::vector<std::string>{"build", "--jobs", "2"});
    ZASSERT(picked.has_value());
    const auto* build = picked->command.get_if<Build>();
    ZASSERT(build != nullptr);
    ZEXPECT(build->jobs == 2);
}

ZEST_CASE(required_field_without_a_word_fails) {
    auto picked = cli::parse<MustPick>(std::vector<std::string>{});
    ZASSERT(!picked.has_value());
    ZEXPECT(picked.error().kind == Error::Kind::MissingSubcommand);
}

ZEST_CASE(two_added_under_one_name_end_the_program, crashes = true) {
    cli::add_command<Late>(+[](Service&) { return 0; }, {.name = "same"});
    cli::add_command<Late>(+[](Service&) { return 1; }, {.name = "same"});
}

ZEST_CASE(added_after_the_set_was_read_ends_the_program, crashes = true) {
    static_cast<void>(cli::parse<LateOptions>(std::vector<std::string>{}));
    cli::add_command<Late>(+[](Service&) { return 0; }, {.name = "late"});
}

ZEST_CASE(subcommand_holding_its_own_set_ends_the_program, crashes = true) {
    static_cast<void>(cli::parse<LoopOptions>(std::vector<std::string>{}));
}

};  // ZEST_SUITE(cli_commands)

}  // namespace

}  // namespace kotatsu::cli
