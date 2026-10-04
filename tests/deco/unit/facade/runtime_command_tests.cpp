#include <cstdint>
#include <expected>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "deco/harness/argv.h"
#include "deco/harness/stdout.h"
#include "deco/harness/text.h"
#include "deco/harness/web_cli.h"
#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco {

namespace {

using test::WebCli;
using strings = std::vector<std::string>;

struct Flow {
    DecoInput(required = false;)
    <std::string> script;

    DecoKV(names = {"--target"}; required = false;)
    <std::string> target;

    DecoFlag(names = {"-v"}; required = false;)
    verbose;
};

struct Leaf {
    DecoKV(names = {"--first"}; required = false;)
    <std::string> token;
};

struct Branch {
    Leaf leaf;
};

struct Nested {
    Branch branch;

    DecoKV(names = {"--second"}; required = false;)
    <std::string> other;
};

struct FieldCallbackCounter {
    inline static int calls = 0;
};

struct Ordered {
    DecoFlag(names = {"-v"}; required = false; after_parsed = [](const Step& step) {
        ++FieldCallbackCounter::calls;
        return step.next();
    };)
    verbose;

    DecoKV(required = false;)
    <std::string> rest;
};

/// A launcher's options: its own, then a script with arguments of the script's own, then
/// "--" and a command to run.
struct Launcher {
    /// A script path, which has to end in ".lua".
    struct ScriptPath {
        std::string path;

        std::optional<std::string> into(std::string_view input, const decl::IntoContext& context) {
            if(!input.ends_with(".lua")) {
                return context.format_error("a lua script is needed");
            }
            path = input;
            return std::nullopt;
        }
    };

    DECO_CFG_START(required = false);

    DecoFlag(names = {"-v"};)
    verbose;

    DecoKV(names = {"-s"}; help = "the path of a script";)
    <ScriptPath> script;

    DECO_CFG_END();

    DecoPack(required = false; help = "the command to run, after '--'";)
    <std::vector<std::string>> command;

    std::vector<std::string> script_args;
};

/// Runs `command` on the argv `line` writes, and returns its exit code.
template <typename T>
int run(cli::Command<T>& command, std::string_view line) {
    auto argv = test::split(line);
    return command(argv);
}

/// A command's options that hold the standard help option beside a required one.
struct Helped {
    decl::HelpOption help;
    DecoKV(names = {"--name"};)
    <std::string> name;
};

/// The help option deeper down.
struct HelpedDeep {
    struct Common {
        decl::HelpOption help;
    };

    Common common;
    DecoKV(names = {"--name"};)
    <std::string> name;
};

/// The usage `command` prints.
template <typename T>
std::string usage_of(const cli::Command<T>& command) {
    std::ostringstream out;
    command.usage(out);
    return out.str();
}

ZEST_SUITE(deco_facade_runtime_command) {

ZEST_CASE(after_hook_sees_the_step) {
    auto command = cli::command<Flow>("flow [OPTIONS]");
    int hits = 0;
    command.after<&Flow::target>([&](auto& step) {
        ++hits;
        EXPECT(step.value() == "t");
        EXPECT(step.arg().spelling == "--target");
        EXPECT(step.arg_index() == 1U);
        EXPECT(step.cursor() == 3U);
        EXPECT(step.next_cursor() == 3U);
        EXPECT(step.argv().size() == 4U);
        EXPECT(step.original_argv().size() == 4U);
        EXPECT(step.trace().size() == 2U);
        EXPECT(strings(step.command_path().begin(), step.command_path().end()) == strings{"flow"});
        EXPECT(step.options().script.as_optional() == std::optional<std::string>("main.lua"));
        EXPECT(step.into_context().highlight_begin() == 1U);
        EXPECT(step.into_context_at_cursor(3).highlight_begin() == 3U);
        EXPECT(zest::contains(step.format_error("stop here"), "at argv[3]:"));
        return step.next();
    });
    auto argv = test::split("main.lua --target t -v");
    const auto parsed = command.invoke(argv);
    ASSERT(parsed.has_value());
    EXPECT(hits == 1);
    EXPECT(parsed->options.verbose.as_optional() == std::optional(true));
}

ZEST_CASE(after_hook_runs_after_the_field_callback) {
    FieldCallbackCounter::calls = 0;
    int hook_calls = 0;
    auto command = cli::command<Ordered>("ordered");
    command.after<&Ordered::verbose>([&](const auto& step) {
        EXPECT(FieldCallbackCounter::calls == 1);
        ++hook_calls;
        return step.stop();
    });
    auto argv = test::split("-v --rest tail");
    const auto parsed = command.invoke(argv);
    ASSERT(parsed.has_value());
    EXPECT(hook_calls == 1);
    EXPECT(!parsed->options.rest.has_value());
    EXPECT(parsed->next_cursor() == 1U);
}

ZEST_CASE(after_hook_reaches_a_nested_member) {
    auto command = cli::command<Nested>("nested");
    std::string seen;
    command.after<&Nested::branch, &Branch::leaf, &Leaf::token>([&](const auto& step) {
        seen = step.value();
        return step.next();
    });
    auto argv = test::split("--second other --first hit");
    const auto parsed = command.invoke(argv);
    ASSERT(parsed.has_value());
    EXPECT(seen == "hit");
    EXPECT(parsed->options.other.as_optional() == std::optional<std::string>("other"));
}

ZEST_CASE(after_hooks_run_in_order_until_one_does_not_go_on) {
    auto command = cli::command<Flow>("flow");
    std::vector<int> ran;
    command
        .after<&Flow::verbose>([&](const auto& step) {
            ran.push_back(1);
            return step.next();
        })
        .after<&Flow::verbose>([&](const auto& step) {
            ran.push_back(2);
            return step.stop();
        })
        .after<&Flow::verbose>([&](const auto& step) {
            ran.push_back(3);
            return step.next();
        });
    auto argv = test::split("-v --target t");
    const auto parsed = command.invoke(argv);
    ASSERT(parsed.has_value());
    EXPECT(ran == (std::vector<int>{1, 2}));
    EXPECT(!parsed->options.target.has_value());
}

ZEST_CASE(after_hook_can_seek_past_what_it_takes) {
    // The script's own arguments go to it, up to "--".
    auto command = cli::command<Launcher>("launcher [OPTIONS] -s <script> [ARGS] -- <command>");
    command.after<&Launcher::script>([](auto& step) {
        auto index = step.next_cursor();
        const auto argv = step.argv();
        while(index < argv.size() && argv[index] != "--") {
            step.options().script_args.push_back(argv[index++]);
        }
        return step.seek(index);
    });
    auto argv = test::split("-v -s run.lua --flag demo -- make test");
    const auto parsed = command.invoke(argv);
    ASSERT(parsed.has_value());
    ASSERT(parsed->options.script.has_value());
    EXPECT(parsed->options.script->path == "run.lua");
    EXPECT(parsed->options.script_args == (strings{"--flag", "demo"}));
    EXPECT(parsed->options.command.as_optional() == std::optional(strings{"make", "test"}));
}

ZEST_CASE(after_hook_bad_value_fails) {
    auto command = cli::command<Launcher>("launcher");
    command.after<&Launcher::script>([](const auto& step) { return step.stop(); });
    auto argv = test::split("-s run.py");
    const auto parsed = command.invoke(argv);
    ASSERT(!parsed.has_value());
    EXPECT(zest::ends_with(parsed.error().message, "a lua script is needed"));
}

ZEST_CASE(after_hook_seeking_past_the_end_ends_the_parse) {
    auto command = cli::command<Flow>("flow");
    command.after<&Flow::verbose>([](const auto& step) { return step.seek(99); });
    auto argv = test::split("-v --target t");
    const auto parsed = command.invoke(argv);
    ASSERT(parsed.has_value());
    EXPECT(!parsed->options.target.has_value());
    EXPECT(parsed->argv().empty());
}

ZEST_CASE(after_hook_can_resume_from_an_argv_of_its_own) {
    auto command = cli::command<Flow>("flow");
    command.after<&Flow::verbose>(
        [](const auto& step) { return step.resume_from(strings{"--target", "own"}); });
    auto argv = test::split("-v --target ignored");
    const auto parsed = command.invoke(argv);
    ASSERT(parsed.has_value());
    EXPECT(parsed->options.target.as_optional() == std::optional<std::string>("own"));
}

ZEST_CASE(after_hook_can_print_the_usage) {
    auto command = cli::command<Flow>("flow [OPTIONS]");
    std::ostringstream printed;
    command.after<&Flow::verbose>([&](const auto& step) {
        step.print_usage(true, printed);
        return step.stop();
    });
    auto argv = test::split("-v");
    ASSERT(command.invoke(argv).has_value());
    EXPECT(printed.str() == usage_of(command));
}

ZEST_CASE(finalizers_run_in_order_after_the_parse) {
    auto command = cli::command<Flow>("flow");
    std::vector<std::string> ran;
    command
        .finalize([&](cli::Invocation<Flow>& invocation) {
            ran.push_back("first");
            invocation.options.target = std::string("default");
        })
        .finalize([&](const cli::Invocation<Flow>& invocation) {
            ran.push_back("second:" + invocation.options.target.value_or(""));
        });
    auto argv = test::split("main.lua");
    const auto parsed = command.invoke(argv);
    ASSERT(parsed.has_value());
    EXPECT(ran == (strings{"first", "second:default"}));
    EXPECT(parsed->options.target.as_optional() == std::optional<std::string>("default"));
}

ZEST_CASE(match_runs_the_handler_of_the_first_category_matched) {
    auto command = cli::command<WebCli>("webcli [OPTIONS]");
    std::string ran;
    command.match(WebCli::version_category, [&](WebCli) { ran = "version"; })
        .match(WebCli::request_category, [&](WebCli options) {
            ran = "request";
            EXPECT(options.request.url.has_value());
        });
    run(command, "--version");
    EXPECT(ran == "version");
    run(command, "-X GET --url https://example.com");
    EXPECT(ran == "request");
}

ZEST_CASE(match_again_replaces_the_handler) {
    auto command = cli::command<WebCli>("webcli");
    std::string ran;
    command.match(WebCli::version_category, [&](WebCli) { ran = "first"; })
        .match(WebCli::version_category, [&](WebCli) { ran = "second"; });
    run(command, "-v");
    EXPECT(ran == "second");
}

ZEST_CASE(match_handler_may_take_the_invocation) {
    auto command = cli::command<WebCli>("webcli");
    std::size_t traced = 0;
    command
        .match(
            WebCli::request_category,
            [&](const cli::Invocation<WebCli>& invocation) { traced = invocation.trace().size(); })
        .match(WebCli::version_category,
               [&](cli::Invocation<WebCli>& invocation) { traced = invocation.trace().size(); })
        .match(WebCli::help_category,
               [&](cli::Invocation<WebCli> invocation) { traced = invocation.trace().size(); });
    run(command, "-X GET --url https://example.com");
    EXPECT(traced == 2U);
    run(command, "-v");
    EXPECT(traced == 1U);
    run(command, "-h");
    EXPECT(traced == 1U);
}

ZEST_CASE(match_all_runs_when_no_category_does) {
    auto command = cli::command<Flow>("flow");
    std::string seen;
    command.match(WebCli::version_category, [&](Flow) { seen = "unrelated"; })
        .match_all([&](Flow options) { seen = options.script.value_or("none"); });
    run(command, "main.lua");
    EXPECT(seen == "main.lua");
}

ZEST_CASE(nothing_runs_when_nothing_matches) {
    auto command = cli::command<WebCli>("webcli");
    bool ran = false;
    command.match(WebCli::version_category, [&](WebCli) { ran = true; });
    EXPECT(run(command, "-h") == 0);
    EXPECT(!ran);
}

ZEST_CASE(handler_exit_code_is_the_command_exit_code) {
    auto command = cli::command<WebCli>("webcli");
    command.match(WebCli::version_category, [](WebCli) { return 4; }).match_all([](WebCli) {
        return 3;
    });
    EXPECT(run(command, "-v") == 4);
    EXPECT(run(command, "-X GET --url https://example.com") == 3);
}

ZEST_CASE(handler_returning_nothing_exits_0) {
    auto command = cli::command<Flow>("flow");
    bool ran = false;
    command.match_all([&](Flow) { ran = true; });
    EXPECT(run(command, "main.lua") == 0);
    EXPECT(ran);
}

ZEST_CASE(execute_of_a_bad_argv_fails) {
    auto command = cli::command<WebCli>("webcli");
    std::optional<cli::ParseError> error;
    bool matched = false;
    command.match_all([&](WebCli) { matched = true; }).on_error([&](cli::ParseError err) {
        error = std::move(err);
    });
    // The usual exit status of a usage error.
    EXPECT(run(command, "--nope") == 2);
    ASSERT(error.has_value());
    EXPECT(error->type == cli::ParseError::Type::BackendParsing);
    EXPECT(!matched);
}

ZEST_CASE(execute_of_a_bad_argv_returns_what_the_error_handler_does) {
    auto command = cli::command<WebCli>("webcli");
    command.on_error([](const cli::ParseError&) { return 5; });
    EXPECT(run(command, "--nope") == 5);
}

ZEST_CASE(execute_of_a_bad_argv_to_a_stream_fails) {
    auto command = cli::command<WebCli>("webcli");
    std::ostringstream errors;
    command.on_error(errors).render_with(test::tagged_renderer());
    auto argv = test::split("--nope");
    EXPECT(command.execute(argv) == 2);
    EXPECT(errors.str() == "ERR<0:unknown option '--nope'>\n");
}

// Given, the help option stops the parse, so the required --name is not
// missed, and prints the usage instead of running a handler.
ZEST_CASE(help_option_prints_the_usage_and_runs_no_handler) {
    auto command = cli::command<Helped>("helped [OPTIONS]");
    bool ran = false;
    command.match_all([&](Helped) { ran = true; });
    for(auto line: {"--help", "-h", "-h --name"}) {
        ZEST_CONTEXT("argv `{}`", line);
        int code = -1;
        auto printed = test::printed_by([&] { return run(command, line); }, code);
        EXPECT(code == 0);
        EXPECT(printed == usage_of(command));
    }
    EXPECT(!ran);
    EXPECT(zest::contains(usage_of(command), "--help"));
}

ZEST_CASE(help_option_counts_at_any_depth) {
    auto command = cli::command<HelpedDeep>("deep");
    bool ran = false;
    command.match_all([&](HelpedDeep) { ran = true; });
    int code = -1;
    auto printed = test::printed_by([&] { return run(command, "--help"); }, code);
    EXPECT(code == 0);
    EXPECT(printed == usage_of(command));
    EXPECT(!ran);
}

// invoke() leaves the help option to its caller, which finds it given.
ZEST_CASE(help_option_after_invoke_is_the_caller_s) {
    auto command = cli::command<Helped>("helped");
    auto argv = test::split("--help");
    std::optional<std::expected<cli::Invocation<Helped>, cli::ParseError>> parsed;
    auto printed = test::printed_by([&] { return command.invoke(argv); }, parsed);
    ASSERT(parsed.has_value());
    ASSERT(parsed->has_value());
    EXPECT((*parsed)->options.help.has_value());
    EXPECT(printed.empty());
}

ZEST_CASE(invocation_renders_the_same_once_moved) {
    // The invocation keeps the renderer its command made for it, rather than pointing at it.
    auto command = cli::command<WebCli>("webcli [OPTIONS]");
    std::string moved_usage;
    std::string moved_error;
    command.match_all([&](cli::Invocation<WebCli> invocation) {
        std::ostringstream out;
        invocation.usage(out);
        moved_usage = out.str();
        moved_error = invocation.format_error("late");
    });
    run(command, "-v");
    EXPECT(moved_usage == usage_of(command));
    EXPECT(zest::starts_with(moved_error, "at end of argv:"));
}

ZEST_CASE(invocation_outlives_its_command) {
    // The command is gone by the end of the statement; its invocation keeps what it renders
    // with.
    auto argv = test::split("-v");
    const auto parsed =
        cli::command<WebCli>("webcli [OPTIONS]").render_with(test::tagged_renderer()).invoke(argv);
    ASSERT(parsed.has_value());
    EXPECT(parsed->command_overview == "webcli [OPTIONS]");
    std::ostringstream out;
    parsed->usage(out);
    EXPECT(out.str() == "USAGE<webcli [OPTIONS]:help>");
    EXPECT(parsed->format_error("late") == "ERR<1:late>");
}

ZEST_CASE(invocation_of_a_command_names_its_command) {
    auto command = cli::command<WebCli>("webcli [OPTIONS]");
    auto argv = test::split("-v");
    const auto parsed = command.invoke(argv);
    ASSERT(parsed.has_value());
    EXPECT(parsed->command_path == strings{"webcli"});
    EXPECT(parsed->command_overview == "webcli [OPTIONS]");
}

ZEST_CASE(invocation_of_a_plain_parse_has_no_usage) {
    auto argv = test::split("-v");
    const auto parsed = cli::parse<WebCli>(argv);
    ASSERT(parsed.has_value());
    std::ostringstream out;
    parsed->usage(out);
    EXPECT(out.str().empty());
}

ZEST_CASE(usage_is_laid_out_by_the_command_renderer) {
    auto command = cli::command<WebCli>("webcli [OPTIONS]");
    command.render_with(test::tagged_renderer());
    EXPECT(usage_of(command) == "USAGE<webcli [OPTIONS]:help>");
}

ZEST_CASE(usage_takes_the_command_renderer_config) {
    auto command = cli::command<WebCli>("webcli [OPTIONS]");
    cli::text::CompatibleRendererConfig config;
    config.usage.options_heading = "Flags:";
    config.usage.group_by_category = false;
    command.render_with_compatible(config);
    const auto usage = usage_of(command);
    EXPECT(zest::contains(usage, "Flags:"));
    EXPECT(!zest::contains(usage, "Group "));

    command.render_with_modern();
    EXPECT(zest::contains(usage_of(command), "\033["));
}

ZEST_CASE(usage_takes_the_command_config) {
    auto command = cli::command<WebCli>("webcli");
    config::BuiltInRenderConfig render;
    render.compatible.usage.options_heading = "Switches:";
    command.config({.enum_meta_var = std::nullopt, .render = render});
    EXPECT(zest::contains(usage_of(command), "Switches:"));
}

ZEST_CASE(default_renderer_beats_the_config) {
    test::ScopedDefaultRenderer restore_renderer;
    test::ScopedDecoConfig restore_config;
    auto config = config::get();
    config.render.compatible.usage.options_heading = "Flags:";
    config::set(config);
    cli::text::set_default_renderer(cli::text::ModernRenderer());

    const auto usage = usage_of(cli::command<WebCli>("webcli"));
    EXPECT(zest::contains(usage, "Usage"));
    EXPECT(!zest::contains(usage, "Flags:"));
}

ZEST_CASE(command_renderer_beats_the_default_renderer) {
    test::ScopedDefaultRenderer restore;
    cli::text::set_default_renderer(cli::text::ModernRenderer());
    auto command = cli::command<WebCli>("webcli [OPTIONS]");
    command.render_with(test::tagged_renderer());
    EXPECT(usage_of(command) == "USAGE<webcli [OPTIONS]:help>");
}

ZEST_CASE(usage_lists_the_options_by_category) {
    const auto usage = usage_of(cli::command<WebCli>("webcli [OPTIONS]"));
    EXPECT_SNAPSHOT(usage);
}

};  // ZEST_SUITE(deco_facade_runtime_command)

}  // namespace

}  // namespace kota::deco
