#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "deco/harness/argv.h"
#include "deco/harness/text.h"
#include "deco/harness/web_cli.h"
#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco {

namespace {

using test::WebCli;
using strings = std::vector<std::string>;

strings joined(std::span<std::string> args) {
    return strings(args.begin(), args.end());
}

/// A tool with "run", "inspect" written "show", and a default route.
struct Tool {
    std::string ran;
    strings args;
    std::optional<cli::SubCommandError> error;
    cli::SubCommander commander{"tool [OPTIONS] <command>", "A build tool"};

    Tool() {
        commander.when_err([this](cli::SubCommandError err) { error = std::move(err); })
            .add(decl::SubCommand{.name = "run", .description = "Run a target"},
                 [this](std::span<std::string> rest) {
                     ran = "run";
                     args = joined(rest);
                 })
            .add(
                decl::SubCommand{
                    .name = "inspect",
                    .description = "Show metadata",
                    .command = "show",
                },
                [this](const cli::SubCommandMatch& match) {
                    ran = match.name + "/" + match.command;
                    args = joined(match.args());
                })
            .add([this](cli::SubCommandMatch match) {
                ran = "default";
                args = joined(match.args());
            });
    }

    Tool(const Tool&) = delete;
    auto operator=(const Tool&) -> Tool& = delete;

    void operator()(std::string_view line) {
        auto argv = test::split(line);
        commander(argv);
    }
};

/// The usage `commander` prints.
std::string usage_of(const cli::SubCommander& commander) {
    std::ostringstream out;
    commander.usage(out);
    return out.str();
}

ZEST_SUITE(deco_facade_runtime_subcommander) {

ZEST_CASE(command_gets_the_arguments_after_it) {
    Tool tool;
    tool("run -v --dry");
    EXPECT(tool.ran == "run");
    EXPECT(tool.args == (strings{"-v", "--dry"}));
    EXPECT(!tool.error.has_value());
}

ZEST_CASE(command_may_be_written_other_than_its_name) {
    Tool tool;
    tool("show x");
    EXPECT(tool.ran == "inspect/show");
    EXPECT(tool.args == strings{"x"});

    tool.ran.clear();
    tool("inspect");
    EXPECT(tool.ran == "default");
}

ZEST_CASE(default_route_gets_every_argument) {
    Tool tool;
    tool("--help me");
    EXPECT(tool.ran == "default");
    EXPECT(tool.args == (strings{"--help", "me"}));
}

ZEST_CASE(match_reports_the_command) {
    Tool tool;
    auto argv = test::split("show x y");
    const auto match = tool.commander.match(argv);
    ASSERT(match.has_value());
    EXPECT(match->is_command());
    EXPECT(!match->is_default());
    EXPECT(match->token == "show");
    EXPECT(match->name == "inspect");
    EXPECT(match->command == "show");
    EXPECT(match->original_argv.size() == 3U);
    EXPECT(joined(match->args()) == (strings{"x", "y"}));
}

ZEST_CASE(match_reports_the_default_route) {
    Tool tool;
    auto argv = test::split("--version");
    const auto match = tool.commander.match(argv);
    ASSERT(match.has_value());
    EXPECT(match->is_default());
    EXPECT(match->token == "--version");
    EXPECT(joined(match->args()) == strings{"--version"});
}

ZEST_CASE(default_route_takes_no_arguments_too) {
    Tool tool;
    std::vector<std::string> argv;
    const auto match = tool.commander.match(argv);
    ASSERT(match.has_value());
    EXPECT(match->is_default());
    EXPECT(match->token.empty());
}

ZEST_CASE(unknown_command_fails) {
    cli::SubCommander commander("tool <command>");
    std::optional<cli::SubCommandError> error;
    commander.add(decl::SubCommand{.name = "run", .description = ""}, [](std::span<std::string>) {})
        .when_err([&](cli::SubCommandError err) { error = std::move(err); });
    auto argv = test::split("walk");
    commander(argv);
    ASSERT(error.has_value());
    EXPECT(error->type == cli::SubCommandError::Type::UnknownSubCommand);
    EXPECT(error->message == "at argv[0]:\n  walk\n  ^~~~\n  unknown subcommand 'walk'");
}

ZEST_CASE(missing_command_fails) {
    cli::SubCommander commander("tool <command>");
    commander.add(decl::SubCommand{.name = "run", .description = ""},
                  [](std::span<std::string>) {});
    std::vector<std::string> argv;
    const auto match = commander.match(argv);
    ASSERT(!match.has_value());
    EXPECT(match.error().type == cli::SubCommandError::Type::MissingSubCommand);
    EXPECT(zest::ends_with(match.error().message, "subcommand is required"));
}

ZEST_CASE(adding_a_command_without_a_name_fails) {
    cli::SubCommander commander("tool <command>");
    std::optional<cli::SubCommandError> error;
    commander.when_err([&](cli::SubCommandError err) { error = std::move(err); })
        .add(decl::SubCommand{.name = "", .description = "", .command = ""},
             [](std::span<std::string>) {});
    ASSERT(error.has_value());
    EXPECT(error->type == cli::SubCommandError::Type::Internal);
    EXPECT(error->message == "subcommand name/command must not be empty");
}

ZEST_CASE(command_without_a_name_is_named_by_its_command) {
    cli::SubCommander commander("tool <command>");
    commander.add(decl::SubCommand{.name = "", .description = "", .command = "go"},
                  [](std::span<std::string>) {});
    auto argv = test::split("go");
    const auto match = commander.match(argv);
    ASSERT(match.has_value());
    EXPECT(match->name == "go");
}

ZEST_CASE(adding_a_command_again_replaces_it_in_place) {
    cli::SubCommander commander("tool <command>");
    std::string ran;
    commander
        .add(decl::SubCommand{.name = "a", .description = ""},
             [&](std::span<std::string>) { ran = "a1"; })
        .add(decl::SubCommand{.name = "b", .description = ""},
             [&](std::span<std::string>) { ran = "b"; })
        .add(decl::SubCommand{.name = "a", .description = "again"},
             [&](std::span<std::string>) { ran = "a2"; })
        .render_with(test::tagged_renderer());
    auto argv = test::split("a");
    commander(argv);
    EXPECT(ran == "a2");
    EXPECT(usage_of(commander) == "SUB<tool <command>:2>");
}

ZEST_CASE(command_object_parses_what_follows_it) {
    std::string url;
    auto web = cli::command<WebCli>("web [OPTIONS]");
    web.match(WebCli::request_category, [&](WebCli options) {
        ASSERT(options.request.url.has_value());
        url = options.request.url->url;
    });

    cli::SubCommander commander("tool <command>");
    commander.add(decl::SubCommand{.name = "web", .description = ""}, web);
    auto argv = test::split("web -X GET --url https://example.com");
    commander(argv);
    EXPECT(url == "https://example.com");
}

ZEST_CASE(command_object_can_be_handed_over) {
    std::string seen;
    auto web = cli::command<WebCli>("web [OPTIONS]");
    web.match_all([&](WebCli) { seen = "web"; });

    cli::SubCommander commander("tool <command>");
    commander.add(decl::SubCommand{.name = "web", .description = ""}, std::move(web));
    auto argv = test::split("web -v");
    commander(argv);
    EXPECT(seen == "web");
}

ZEST_CASE(handler_returns_the_exit_code) {
    cli::SubCommander commander("tool <command>");
    commander
        .add(decl::SubCommand{.name = "fail", .description = ""},
             [](std::span<std::string>) { return 3; })
        .add(decl::SubCommand{.name = "pass", .description = ""}, [](std::span<std::string>) {})
        .add([](const cli::SubCommandMatch&) { return 4; });
    auto fail = test::split("fail");
    EXPECT(commander(fail) == 3);
    auto pass = test::split("pass");
    EXPECT(commander(pass) == 0);
    auto other = test::split("other");
    EXPECT(commander.parse(other) == 4);
}

ZEST_CASE(command_object_returns_its_exit_code) {
    auto web = cli::command<WebCli>("web [OPTIONS]");
    web.match_all([](WebCli) { return 6; });
    cli::SubCommander commander("tool <command>");
    commander.add(decl::SubCommand{.name = "web", .description = ""}, std::move(web));
    auto argv = test::split("web -v");
    EXPECT(commander(argv) == 6);
}

ZEST_CASE(unknown_or_missing_command_exits_with_the_parse_error_code) {
    cli::SubCommander commander("tool <command>");
    std::ostringstream errors;
    commander.when_err(errors).add(decl::SubCommand{.name = "run", .description = ""},
                                   [](std::span<std::string>) { return 0; });
    auto walk = test::split("walk");
    EXPECT(commander(walk) == cli::parse_error_exit_code);
    EXPECT(zest::contains(errors.str(), "unknown subcommand 'walk'"));
    std::vector<std::string> none;
    EXPECT(commander(none) == cli::parse_error_exit_code);
    EXPECT(zest::contains(errors.str(), "subcommand is required"));
}

ZEST_CASE(unknown_command_to_a_stream_fails) {
    cli::SubCommander commander("tool <command>");
    std::ostringstream errors;
    commander.render_with(test::tagged_renderer()).when_err(errors);
    commander.add(decl::SubCommand{.name = "run", .description = ""},
                  [](std::span<std::string>) {});
    auto argv = test::split("walk");
    commander.parse(argv);
    EXPECT(errors.str() == "ERR<0:unknown subcommand 'walk'>\n");
}

ZEST_CASE(usage_lists_the_commands) {
    Tool tool;
    EXPECT_SNAPSHOT(usage_of(tool.commander));
}

ZEST_CASE(usage_shows_the_usage_line_only_with_a_default) {
    cli::SubCommander commander("tool <command>", "A build tool");
    commander.add(decl::SubCommand{.name = "run", .description = ""},
                  [](std::span<std::string>) {});
    EXPECT(!zest::contains(usage_of(commander), "usage: tool <command>"));
    commander.add([](std::span<std::string>) {});
    EXPECT(zest::contains(usage_of(commander), "usage: tool <command>"));
}

ZEST_CASE(usage_takes_the_renderer_and_config) {
    Tool tool;
    tool.commander.render_with_modern();
    EXPECT(zest::contains(usage_of(tool.commander), "Commands"));

    config::BuiltInRenderConfig render;
    render.compatible.subcommand.heading = "Verbs:";
    tool.commander.render_with_compatible(render.compatible);
    EXPECT(zest::contains(usage_of(tool.commander), "Verbs:"));
}

ZEST_CASE(usage_takes_the_config_of_the_commander) {
    test::ScopedDefaultRenderer restore;
    cli::text::clear_default_renderer();
    Tool tool;
    config::BuiltInRenderConfig render;
    render.compatible.subcommand.heading = "Verbs:";
    tool.commander.config({.enum_meta_var = std::nullopt, .render = render});
    EXPECT(zest::contains(usage_of(tool.commander), "Verbs:"));
}

};  // ZEST_SUITE(deco_facade_runtime_subcommander)

}  // namespace

}  // namespace kota::deco
