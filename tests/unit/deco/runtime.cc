#include "eventide/deco/detail/runtime.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <print>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "eventide/deco/deco.h"
#include "eventide/deco/detail/macro.h"
#include "eventide/deco/detail/text.h"
#include "eventide/zest/zest.h"
#include <eventide/zest/macro.h>

namespace {

struct Version {
    DecoFlag(names = {"-v", "--version"}, help = "Show version and exit")
    version;
};

struct Help {
    DecoFlag(names = {"-h", "--help"}, help = "Show this help message and exit")
    help;
};

struct Request {
    struct RequestType {
        constexpr RequestType() = default;
        constexpr ~RequestType() = default;

        enum class Type {
            Get,
            Post,
        } type;

        std::optional<std::string> into(std::string_view input) {
            if(input == "GET" || input == "POST") {
                type = (input == "GET") ? Type::Get : Type::Post;
                return std::nullopt;
            } else {
                return "Invalid request type. Expected 'GET' or 'POST'.";
            }
        }
    };

    struct Url {
        constexpr Url() = default;
        constexpr ~Url() = default;

        std::string url;

        std::optional<std::string> into(std::string_view input) {
            if(input.starts_with("http://") || input.starts_with("https://")) {
                url = std::string(input);
                return std::nullopt;
            } else {
                return "Invalid URL. Expected to start with 'http://' or 'https://'.";
            }
        }
    };

    DecoFlag(help = "Enable verbose output", required = false)
    verbose = false;

    DecoKV(names = {"-X", "--type"}, meta_var = "<Method>")
    <RequestType> method;

    DecoKV(meta_var = "<URL>", help = "Request URL")
    <Url> url;
};

struct WebCliOpt {
    struct Cate {
        constexpr static deco::decl::Category version_category{
            .exclusive = true,
            .required = false,
            .name = "version",
            .description = "version-only mode",
        };
        constexpr static deco::decl::Category help_category{
            .exclusive = true,
            .required = false,
            .name = "help",
            .description = "help-only mode",
        };
        constexpr static deco::decl::Category request_category{
            .exclusive = true,
            .required = false,
            .name = "request",
            .description = "request options",
        };
    };

    DECO_CFG(required = false, category = Cate::version_category);
    Version version;

    DECO_CFG(required = true, category = Cate::request_category);
    Request request;

    DECO_CFG(required = false, category = Cate::help_category);
    Help help;
};

struct InputAndTrailingOpt {
    struct Cate {
        constexpr static deco::decl::Category input_category{
            .exclusive = false,
            .required = false,
            .name = "input",
            .description = "single positional input",
        };
        constexpr static deco::decl::Category trailing_category{
            .exclusive = false,
            .required = false,
            .name = "trailing",
            .description = "all arguments after --",
        };
    };

    DecoInput(required = false; category = Cate::input_category;)
    <std::string> input;
    DecoPack(required = false; category = Cate::trailing_category;)
    <std::vector<std::string>> trailing;
};

struct TrailingOnlyOpt {
    DecoPack(required = false)
    <std::vector<std::string>> trailing;
};

struct CallbackStopState {
    inline static unsigned arg_index = 0;
    inline static unsigned next_cursor = 0;
    inline static std::size_t argv_size = 0;
    inline static std::string value;

    static void reset() {
        arg_index = 0;
        next_cursor = 0;
        argv_size = 0;
        value.clear();
    }
};

struct CallbackStopOpt {
    DecoInput(required = false; after_parsed = [](const Step& step) {
        CallbackStopState::arg_index = step.arg().index;
        CallbackStopState::next_cursor = step.next_cursor();
        CallbackStopState::argv_size = step.argv().size();
        CallbackStopState::value = step.value();
        return step.stop();
    };)
    <std::string> script;

    DecoKV(required = true)
    <std::string> required_after_stop;
};

struct CallbackRestartState {
    inline static unsigned arg_index = 0;
    inline static unsigned next_cursor = 0;
    inline static std::string value;

    static void reset() {
        arg_index = 0;
        next_cursor = 0;
        value.clear();
    }
};

struct CallbackRestartOpt {
    DecoInput(required = false; after_parsed = [](const Step& step) {
        CallbackRestartState::arg_index = step.arg().index;
        CallbackRestartState::next_cursor = step.next_cursor();
        CallbackRestartState::value = step.value();
        return step.restart(step.argv().subspan(step.next_cursor() + 2));
    };)
    <std::string> script;

    DecoKV(names = {"--skip"}; required = false)
    <std::string> skip;

    DecoFlag(names = {"-v"}; required = false)
    verbose;
};

struct CallbackRestartOwnedState {
    inline static unsigned arg_index = 0;
    inline static unsigned next_cursor = 0;
    inline static std::string value;

    static void reset() {
        arg_index = 0;
        next_cursor = 0;
        value.clear();
    }
};

struct CallbackRestartOwnedOpt {
    DecoInput(required = false; after_parsed = [](const Step& step) {
        CallbackRestartOwnedState::arg_index = step.arg().index;
        CallbackRestartOwnedState::next_cursor = step.next_cursor();
        CallbackRestartOwnedState::value = step.value();
        std::vector<std::string> rewritten;
        rewritten.emplace_back("-v");
        return step.restart(rewritten);
    };)
    <std::string> script;

    DecoFlag(names = {"-v"}; required = false)
    verbose;
};

struct CallbackRestartTwiceState {
    inline static unsigned restart_count = 0;

    static void reset() {
        restart_count = 0;
    }
};

struct CallbackRestartTwiceOpt {
    DecoInput(required = false; after_parsed = [](const Step& step) {
        if(CallbackRestartTwiceState::restart_count == 0) {
            ++CallbackRestartTwiceState::restart_count;
            return step.restart(std::vector<std::string>{"second.cc"});
        }
        if(CallbackRestartTwiceState::restart_count == 1) {
            ++CallbackRestartTwiceState::restart_count;
            return step.restart(std::vector<std::string>{"--name", "final"});
        }
        return step.next();
    };)
    <std::string> script;

    DecoKV(names = {"--name"}; required = false)
    <std::string> name;
};

struct CallbackShortcutOpt {
    DecoInput(required = false; after_parsed = Action::stop;)
    <std::string> script;

    DecoKV(required = true)
    <std::string> required_after_stop;
};

struct CallbackComposeState {
    inline static unsigned arg_index = 0;
    inline static unsigned next_cursor = 0;
    inline static std::size_t argv_size = 0;
    inline static bool value = false;
    inline static unsigned count = 0;

    static void reset() {
        arg_index = 0;
        next_cursor = 0;
        argv_size = 0;
        value = false;
        count = 0;
    }
};

struct CallbackComposeOpt {
    DecoFlag(names = {"-v"}; required = false; after_parsed = [](const Step& step) {
        CallbackComposeState::arg_index = step.arg().index;
        CallbackComposeState::next_cursor = step.next_cursor();
        CallbackComposeState::argv_size = step.argv().size();
        CallbackComposeState::value = step.value();
        ++CallbackComposeState::count;
        return step.next();
    };)
    verbose;

    DecoKV(required = false)
    <std::string> rest;
};

struct CommandFlowOpt {
    DecoInput(required = false)
    <std::string> script;

    DecoKV(names = {"--target"}; required = false)
    <std::string> target;
};

struct NestedAfterFirstLeaf {
    DecoKV(names = {"--first-token"}; required = false)
    <std::string> token;
};

struct NestedAfterSecondLeaf {
    DecoKV(names = {"--second-token"}; required = false)
    <std::string> token;
};

struct NestedAfterFirstBranch {
    NestedAfterFirstLeaf leaf;
};

struct NestedAfterSecondBranch {
    NestedAfterSecondLeaf leaf;
};

struct NestedAfterOpt {
    NestedAfterFirstBranch first;
    NestedAfterSecondBranch second;
};

}  // namespace

namespace {

template <typename... Args>
std::span<std::string> into_deco_args(Args&&... args) {
    static std::vector<std::string> res;
    res.clear();
    res.reserve(sizeof...(args));
    (res.emplace_back(std::forward<Args>(args)), ...);
    return res;
}

struct ScopedDefaultRenderer {
    deco::cli::text::Renderer saved = deco::cli::text::default_renderer();

    ~ScopedDefaultRenderer() {
        deco::cli::text::set_default_renderer(saved);
    }
};

auto make_custom_renderer() -> deco::cli::text::Renderer {
    auto renderer = deco::cli::text::CompatibleRenderer();
    renderer.usage = [](const deco::cli::text::UsageDocument& document,
                        bool include_help,
                        const deco::cli::text::TextStyle&) {
        return std::format("USAGE<{}:{}>", document.overview, include_help ? "help" : "plain");
    };
    renderer.subcommand = [](const deco::cli::text::SubCommandDocument& document,
                             const deco::cli::text::TextStyle&) {
        return std::format("SUB<{}:{}>", document.usage_line, document.entries.size());
    };
    renderer.diagnostic = [](const deco::cli::text::Diagnostic& diagnostic,
                             const deco::cli::text::TextStyle&) {
        return std::format("ERR<{}:{}>", diagnostic.begin, diagnostic.message);
    };
    return renderer;
}

};  // namespace

struct CatterSelf {
    DecoFlag(required = false)
    v;
    DecoInput(required = false)
    <std::string> script_internal;
    DecoKV(required = false)
    <std::string> s;
};

struct CatterTrailing {
    DecoInput(required = false)
    <std::vector<std::string>> script_args;
    DecoPack(required = false)
    <std::vector<std::string>> cmd;
};

TEST_SUITE(cli_parse) {

TEST_CASE(parsing) {
    auto args = into_deco_args("-X", "POST", "--url", "https://example.com");
    auto res = deco::cli::parse<WebCliOpt>(args);
    EXPECT_TRUE(res.has_value());

    const auto& opt = res->options;
    EXPECT_TRUE(opt.request.method->type == Request::RequestType::Type::Post);
    EXPECT_TRUE(opt.request.url->url == "https://example.com");
}

TEST_CASE(parse_result_exposes_options) {
    auto args = into_deco_args("-X", "POST", "--url", "https://example.com");
    auto res = deco::cli::parse<WebCliOpt>(args);
    EXPECT_TRUE(res.has_value());
    if(!res.has_value()) {
        return;
    }

    EXPECT_TRUE(res->options.request.method->type == Request::RequestType::Type::Post);
    EXPECT_TRUE(res->options.request.url->url == "https://example.com");
}

TEST_CASE(parsing_input_and_trailing) {
    auto args = into_deco_args("front", "--", "a", "b", "c");
    auto res = deco::cli::parse<InputAndTrailingOpt>(args);
    EXPECT_TRUE(res.has_value());
    if(!res.has_value()) {
        return;
    }
    const auto& opt = res->options;
    EXPECT_TRUE(*opt.input == "front");
    EXPECT_TRUE(opt.trailing->size() == 3);
    EXPECT_TRUE((*opt.trailing)[0] == "a");
    EXPECT_TRUE((*opt.trailing)[1] == "b");
    EXPECT_TRUE((*opt.trailing)[2] == "c");
    EXPECT_TRUE(res->matched_categories.contains(&InputAndTrailingOpt::Cate::input_category));
    EXPECT_TRUE(res->matched_categories.contains(&InputAndTrailingOpt::Cate::trailing_category));
}

TEST_CASE(parsing_trailing_requires_dash_dash_separator) {
    auto bad = deco::cli::parse<TrailingOnlyOpt>(into_deco_args("front"));
    EXPECT_FALSE(bad.has_value());
    EXPECT_TRUE(bad.error().type == deco::cli::ParseError::Type::DecoParsing);
    EXPECT_TRUE(bad.error().message.contains("unexpected input argument"));

    auto good = deco::cli::parse<TrailingOnlyOpt>(into_deco_args("--", "a", "b"));
    EXPECT_TRUE(good.has_value());
    if(!good.has_value()) {
        return;
    }
    EXPECT_TRUE(good->options.trailing.has_value());
    EXPECT_TRUE(good->options.trailing->size() == 2);
    EXPECT_TRUE((*good->options.trailing)[0] == "a");
    EXPECT_TRUE((*good->options.trailing)[1] == "b");
}

TEST_CASE(when_error) {
    auto res = deco::cli::parse<WebCliOpt>(into_deco_args("-X", "INVALID"));
    EXPECT_FALSE(res.has_value());
    EXPECT_TRUE(res.error().type == deco::cli::ParseError::Type::IntoError &&
                res.error().message.contains("Invalid request type"));

    auto res2 = deco::cli::parse<WebCliOpt>(into_deco_args("--url", "ftp://example.com"));
    EXPECT_FALSE(res2.has_value());
    EXPECT_TRUE(res2.error().type == deco::cli::ParseError::Type::IntoError &&
                res2.error().message.contains("Invalid URL"));

    auto res3 = deco::cli::parse<WebCliOpt>(into_deco_args("--unknown"));
    EXPECT_FALSE(res3.has_value());
    EXPECT_TRUE(res3.error().type == deco::cli::ParseError::Type::BackendParsing &&
                res3.error().message.contains("unknown option"));

    auto res4 = deco::cli::parse<WebCliOpt>(into_deco_args("-v", "--help"));
    EXPECT_FALSE(res4.has_value());
    EXPECT_TRUE(res4.error().type == deco::cli::ParseError::Type::DecoParsing &&
                res4.error().message.contains("exclusive"));

    auto res5 = deco::cli::parse<WebCliOpt>(into_deco_args("-X", "GET"));
    EXPECT_FALSE(res5.has_value());
    EXPECT_TRUE(res5.error().type == deco::cli::ParseError::Type::DecoParsing &&
                res5.error().message.contains("required option"));

    auto res6 = deco::cli::parse<WebCliOpt>(into_deco_args("--", "a", "b"));
    EXPECT_FALSE(res6.has_value());
    EXPECT_TRUE(res6.error().type == deco::cli::ParseError::Type::BackendParsing &&
                res6.error().message.contains("unknown option"));
}

TEST_CASE(parse_errors_include_location_context) {
    auto res = deco::cli::parse<WebCliOpt>(into_deco_args("--unknown"));
    EXPECT_FALSE(res.has_value());
    if(res.has_value()) {
        return;
    }

    EXPECT_TRUE(res.error().message.contains("at argv[0]:"));
    EXPECT_TRUE(res.error().message.contains("--unknown"));
    EXPECT_TRUE(res.error().message.contains("^"));
}

TEST_CASE(global_text_style_can_disable_positioned_diagnostics) {
    ScopedDefaultRenderer restore;
    auto style = deco::cli::text::default_text_style();
    style.diagnostic.enabled = false;
    deco::cli::text::set_default_text_style(style);

    auto res = deco::cli::parse<WebCliOpt>(into_deco_args("--unknown"));
    EXPECT_FALSE(res.has_value());
    if(res.has_value()) {
        return;
    }

    EXPECT_TRUE(!res.error().message.contains("at argv["));
    EXPECT_TRUE(!res.error().message.contains("^"));
    EXPECT_TRUE(res.error().message == "unknown option '--unknown'");
}

TEST_CASE(parse_overload_accepts_custom_renderer) {
    auto renderer = make_custom_renderer();
    auto res = deco::cli::parse<WebCliOpt>(into_deco_args("--unknown"), renderer);
    EXPECT_FALSE(res.has_value());
    if(res.has_value()) {
        return;
    }

    EXPECT_TRUE(res.error().message == "ERR<0:unknown option '--unknown'>");
}

TEST_CASE(diagnostic_at_uses_non_owning_argv_view) {
    auto argv = into_deco_args("--unknown", "value");
    const auto argv_view = std::span<const std::string>(argv.data(), argv.size());
    auto diagnostic = deco::cli::text::diagnostic_at(argv_view, 0, 1, "boom");

    EXPECT_EQ(diagnostic.argv.data(), argv.data());
    argv[0] = "--renamed";

    auto renderer = deco::cli::text::CompatibleRenderer();
    const auto rendered = deco::cli::text::render_diagnostic(diagnostic, &renderer);
    EXPECT_TRUE(rendered.contains("--renamed"));
}

TEST_CASE(modern_renderer_highlights_usage_and_diagnostic) {
    auto renderer = deco::cli::text::ModernRenderer();
    const auto usage_document = deco::cli::text::UsageDocument{
        .overview = "webcli [OPTIONS]",
        .groups =
            {
                     deco::cli::text::UsageGroup{
                    .title = "network",
                    .entries =
                        {
                            deco::cli::text::UsageEntry{
                                .usage = "--host <value>",
                                .help = "connect target",
                            },
                        },
                }, },
    };

    const auto usage = deco::cli::text::render_usage(usage_document, true, &renderer);
    EXPECT_TRUE(usage.contains("\033["));
    EXPECT_TRUE(usage.contains("Usage"));
    EXPECT_TRUE(usage.contains("Options"));
    EXPECT_TRUE(usage.contains("\033[39mconnect target\033[0m"));
    EXPECT_TRUE(usage.contains("\033[1;4;38;5;110mnetwork\033[0m"));

    auto argv = into_deco_args("webcli", "--unknown");
    const auto diagnostic = deco::cli::text::render_diagnostic(
        deco::cli::text::diagnostic_at(std::span<const std::string>(argv.data(), argv.size()),
                                       1,
                                       2,
                                       "boom"),
        &renderer);
    EXPECT_TRUE(diagnostic.contains("\033["));
    EXPECT_TRUE(diagnostic.contains("╰─▶"));
    EXPECT_TRUE(diagnostic.contains("\033[39m[argv[1]]\033[0m"));
}

TEST_CASE(modern_renderer_crops_long_diagnostic_source_line) {
    auto renderer = deco::cli::text::ModernRenderer();

    const std::string very_long_a(200, 'a');
    const std::string very_long_b(220, 'b');
    auto argv = into_deco_args("-s", very_long_a, very_long_b, "--", "make");
    const auto diagnostic = deco::cli::text::render_diagnostic(
        deco::cli::text::diagnostic_at(std::span<const std::string>(argv.data(), argv.size()),
                                       1,
                                       2,
                                       "too long"),
        &renderer);

    EXPECT_TRUE(diagnostic.contains("..."));
    EXPECT_TRUE(diagnostic.contains("╰─▶"));
    EXPECT_TRUE(diagnostic.contains("too long"));
    EXPECT_TRUE(!diagnostic.contains(very_long_b));
}

TEST_CASE(with_cont_parse) {
    std::vector<std::string> args = {"-v", "script::cdb", "-t", "x", "--", "make"};
    auto res = deco::cli::parse_with_callback<CatterSelf>(
        args,
        [](const CatterSelf& opt, deco::decl::DecoOptionBase* ptr) {
            return !(&opt.s == ptr || &opt.script_internal == ptr);
        });
    auto res2 = deco::cli::parse<CatterTrailing>({args.begin() + res->next_index, args.end()});
    EXPECT_EQ(res->next_index, 2);
    EXPECT_EQ(*res->options.script_internal, "script::cdb");
    EXPECT_EQ(res2->options.cmd->size(), 1);
    EXPECT_EQ((*res2->options.script_args)[0], "-t");
}

TEST_CASE(invocation_exposes_trace_and_remaining_args) {
    std::vector<std::string> args = {"-v", "script::cdb", "-t", "x", "--", "make"};
    auto command = deco::cli::command<CatterSelf>("catter");
    command.after<&CatterSelf::script_internal>([](const auto& step) { return step.stop(); });
    auto res = command.invoke(args);
    EXPECT_TRUE(res.has_value());
    if(!res.has_value()) {
        return;
    }

    EXPECT_EQ(res->next_cursor(), 2u);
    EXPECT_EQ(res->argv().size(), 6u);
    EXPECT_EQ(res->remaining().size(), 4u);
    EXPECT_TRUE(res->remaining()[0] == "-t");
    EXPECT_EQ(res->trace().size(), 2u);
    EXPECT_TRUE(res->trace()[0].get_spelling_view() == "-v");
    EXPECT_TRUE(res->trace()[1].get_spelling_view() == "script::cdb");
}

TEST_CASE(option_callback_can_stop_early_with_current_result) {
    CallbackStopState::reset();

    auto res = deco::cli::parse<CallbackStopOpt>(into_deco_args("script.lua"));
    EXPECT_TRUE(res.has_value());
    if(!res.has_value()) {
        return;
    }

    EXPECT_TRUE(res->options.script.has_value());
    EXPECT_TRUE(*res->options.script == "script.lua");
    EXPECT_TRUE(!res->options.required_after_stop.has_value());
    EXPECT_EQ(res->next_index, 1);
    EXPECT_EQ(CallbackStopState::arg_index, 0u);
    EXPECT_EQ(CallbackStopState::next_cursor, 1u);
    EXPECT_EQ(CallbackStopState::argv_size, 1u);
    EXPECT_TRUE(CallbackStopState::value == "script.lua");
}

TEST_CASE(option_callback_can_restart_with_new_span) {
    CallbackRestartState::reset();

    auto res =
        deco::cli::parse<CallbackRestartOpt>(into_deco_args("entry.cc", "--skip", "ignored", "-v"));
    EXPECT_TRUE(res.has_value());
    if(!res.has_value()) {
        return;
    }

    EXPECT_TRUE(res->options.script.has_value());
    EXPECT_TRUE(*res->options.script == "entry.cc");
    EXPECT_TRUE(!res->options.skip.has_value());
    EXPECT_TRUE(res->options.verbose.has_value() && *res->options.verbose);
    EXPECT_EQ(res->next_index, 1);
    EXPECT_EQ(CallbackRestartState::arg_index, 0u);
    EXPECT_EQ(CallbackRestartState::next_cursor, 1u);
    EXPECT_TRUE(CallbackRestartState::value == "entry.cc");
}

TEST_CASE(option_callback_can_restart_with_owned_argv) {
    CallbackRestartOwnedState::reset();

    auto res = deco::cli::parse<CallbackRestartOwnedOpt>(into_deco_args("entry.cc"));
    EXPECT_TRUE(res.has_value());
    if(!res.has_value()) {
        return;
    }

    EXPECT_TRUE(res->options.script.has_value());
    EXPECT_TRUE(*res->options.script == "entry.cc");
    EXPECT_TRUE(res->options.verbose.has_value() && *res->options.verbose);
    EXPECT_EQ(res->argv().size(), 1u);
    EXPECT_TRUE(res->argv()[0] == "-v");
    EXPECT_EQ(res->next_index, 1);
    EXPECT_EQ(CallbackRestartOwnedState::arg_index, 0u);
    EXPECT_EQ(CallbackRestartOwnedState::next_cursor, 1u);
    EXPECT_TRUE(CallbackRestartOwnedState::value == "entry.cc");
}

TEST_CASE(option_callback_can_restart_multiple_times_with_owned_argv) {
    CallbackRestartTwiceState::reset();

    auto res = deco::cli::parse<CallbackRestartTwiceOpt>(into_deco_args("first.cc"));
    EXPECT_TRUE(res.has_value());
    if(!res.has_value()) {
        return;
    }

    EXPECT_EQ(CallbackRestartTwiceState::restart_count, 2u);
    EXPECT_TRUE(res->options.script.has_value());
    EXPECT_TRUE(*res->options.script == "second.cc");
    EXPECT_TRUE(res->options.name.has_value());
    EXPECT_TRUE(*res->options.name == "final");
    EXPECT_EQ(res->argv().size(), 2u);
    EXPECT_TRUE(res->argv()[0] == "--name");
    EXPECT_TRUE(res->argv()[1] == "final");
}

TEST_CASE(option_callback_supports_action_shortcut) {
    auto res = deco::cli::parse<CallbackShortcutOpt>(into_deco_args("shortcut.lua"));
    EXPECT_TRUE(res.has_value());
    if(!res.has_value()) {
        return;
    }

    EXPECT_TRUE(res->options.script.has_value());
    EXPECT_TRUE(*res->options.script == "shortcut.lua");
    EXPECT_TRUE(!res->options.required_after_stop.has_value());
    EXPECT_EQ(res->next_index, 1);
}

TEST_CASE(command_after_runs_after_field_callback) {
    CallbackComposeState::reset();
    unsigned command_count = 0;

    auto command = deco::cli::command<CallbackComposeOpt>("compose");
    command.after<&CallbackComposeOpt::verbose>([&](const auto& step) {
        EXPECT_EQ(CallbackComposeState::count, 1u);
        EXPECT_TRUE(step.value());
        ++command_count;
        return step.stop();
    });

    auto res = command.invoke(into_deco_args("-v", "--rest", "tail"));
    EXPECT_TRUE(res.has_value());
    if(!res.has_value()) {
        return;
    }

    EXPECT_TRUE(res->options.verbose.has_value() && *res->options.verbose);
    EXPECT_TRUE(!res->options.rest.has_value());
    EXPECT_EQ(res->next_index, 1);
    EXPECT_EQ(CallbackComposeState::count, 1u);
    EXPECT_EQ(command_count, 1u);
    EXPECT_EQ(CallbackComposeState::arg_index, 0u);
    EXPECT_EQ(CallbackComposeState::next_cursor, 1u);
    EXPECT_EQ(CallbackComposeState::argv_size, 3u);
    EXPECT_TRUE(CallbackComposeState::value);
}

TEST_CASE(command_after_supports_nested_member_paths) {
    unsigned hit_count = 0;
    std::string seen;

    auto command = deco::cli::command<NestedAfterOpt>("nested");
    command
        .after<&NestedAfterOpt::first, &NestedAfterFirstBranch::leaf, &NestedAfterFirstLeaf::token>(
            [&](const auto& step) {
                ++hit_count;
                seen = step.value();
                EXPECT_TRUE(step.arg().get_spelling_view() == "--first-token");
                return step.next();
            });

    auto res = command.invoke(into_deco_args("--second-token", "other", "--first-token", "hit"));
    EXPECT_TRUE(res.has_value());
    if(!res.has_value()) {
        return;
    }

    EXPECT_TRUE(res->options.first.leaf.token.has_value());
    EXPECT_TRUE(res->options.second.leaf.token.has_value());
    EXPECT_TRUE(*res->options.first.leaf.token == "hit");
    EXPECT_TRUE(*res->options.second.leaf.token == "other");
    EXPECT_EQ(hit_count, 1u);
    EXPECT_TRUE(seen == "hit");
}

TEST_CASE(command_callbacks_can_capture_state_finalize_and_match_all) {
    std::string seen;
    std::string entry;
    auto command = deco::cli::command<CommandFlowOpt>("run");
    command
        .after<&CommandFlowOpt::script>([prefix = std::string("entry:"), &entry](auto& step) {
            entry = prefix + step.value();
            return step.next();
        })
        .finalize([](auto& ctx) {
            if(!ctx.options.target.has_value()) {
                ctx.options.target = std::string("default");
            }
        })
        .matchAll([&](auto& ctx) { seen = entry + "|" + ctx.options.target.value(); });

    command(into_deco_args("main.lua"));
    EXPECT_TRUE(seen == "entry:main.lua|default");
}

};  // TEST_SUITE(cli_parse)

TEST_SUITE(command_match) {

TEST_CASE(match_dispatches_by_category) {
    auto command = deco::cli::command<WebCliOpt>("webcli [OPTIONS]");
    std::stringstream ss;
    command.match(WebCliOpt::Cate::version_category, [&](auto) { ss << "Version 1.0.0"; })
        .match(WebCliOpt::Cate::help_category, [&](auto) { command.usage(ss, true); })
        .match(WebCliOpt::Cate::request_category,
               [&](WebCliOpt opt) {
                   EXPECT_TRUE(opt.request.method.has_value());
                   EXPECT_TRUE(opt.request.url.has_value());
               })
        .on_error([&](auto err) { ss << "Error: " << err.message << "\n"; });

    command(into_deco_args("-v"));
    EXPECT_TRUE(ss.str().contains("Version 1.0.0"));

    ss.str("");
    command(into_deco_args("--help"));
    EXPECT_TRUE(ss.str().contains("webcli [OPTIONS]"));

    ss.str("");
    command(into_deco_args("-X", "GET", "--url", "https://example.com"));
}

TEST_CASE(match_can_observe_invocation_context) {
    auto command = deco::cli::command<WebCliOpt>("webcli [OPTIONS]");
    std::string seen_url;
    unsigned seen_trace_size = 0;
    command.match(WebCliOpt::Cate::request_category,
                  [&](const deco::cli::Invocation<WebCliOpt>& invocation) {
                      EXPECT_TRUE(invocation.matched(WebCliOpt::Cate::request_category));
                      seen_trace_size = invocation.trace().size();
                      seen_url = invocation.options.request.url->url;
                  });

    command(into_deco_args("-X", "GET", "--url", "https://example.com"));
    EXPECT_TRUE(seen_url == "https://example.com");
    EXPECT_EQ(seen_trace_size, 2u);
}

TEST_CASE(command_usage_respects_text_style_override) {
    auto command = deco::cli::command<WebCliOpt>("webcli [OPTIONS]");
    auto style = deco::cli::text::default_text_style();
    style.usage.options_heading = "Flags:";
    style.usage.group_by_category = false;
    command.text_style(style);

    std::stringstream ss;
    command.usage(ss);
    EXPECT_TRUE(ss.str().contains("Flags:"));
    EXPECT_TRUE(!ss.str().contains("Options:"));
}

TEST_CASE(command_can_use_custom_renderer) {
    std::string seen_error;
    auto command = deco::cli::command<WebCliOpt>("webcli [OPTIONS]");
    command.render_with(make_custom_renderer()).on_error([&](auto err) {
        seen_error = err.message;
    });

    std::stringstream ss;
    command.usage(ss);
    EXPECT_TRUE(ss.str() == "USAGE<webcli [OPTIONS]:help>");

    command(into_deco_args("--unknown"));
    EXPECT_TRUE(seen_error == "ERR<0:unknown option '--unknown'>");
}

};  // TEST_SUITE(command_match)

TEST_SUITE(subcommander) {

TEST_CASE(dispatching_with_subcommand_and_default) {
    deco::cli::SubCommander subcommander("catter [OPTIONS]");
    std::stringstream ss;
    subcommander
        .add(
            deco::decl::SubCommand{
                .name = "run",
                .description = "Run task",
            },
            [&](std::span<std::string> args) {
                ss << "run:";
                for(const auto& arg: args) {
                    ss << arg << ",";
                }
            })
        .add([&](std::span<std::string> args) {
            ss << "default:";
            for(const auto& arg: args) {
                ss << arg << ",";
            }
        })
        .when_err([&](auto err) { ss << "err:" << err.message; });

    std::vector<std::string> run_args = {"run", "-v", "--dry"};
    subcommander(run_args);
    EXPECT_TRUE(ss.str() == "run:-v,--dry,");

    ss.str("");
    ss.clear();

    std::vector<std::string> default_args = {"--help"};
    subcommander(default_args);
    EXPECT_TRUE(ss.str() == "default:--help,");
}

TEST_CASE(match_reports_subcommand_context) {
    deco::cli::SubCommander subcommander("catter [OPTIONS]");
    subcommander.add(
        deco::decl::SubCommand{
            .name = "run",
            .description = "Run task",
        },
        [](std::span<std::string>) {});

    auto match = subcommander.match(into_deco_args("run", "-v", "--dry"));
    EXPECT_TRUE(match.has_value());
    if(!match.has_value()) {
        return;
    }

    EXPECT_TRUE(match->is_command());
    EXPECT_TRUE(match->command == "run");
    EXPECT_TRUE(match->name == "run");
    EXPECT_EQ(match->args().size(), 2u);
    EXPECT_TRUE(match->args()[0] == "-v");
}

TEST_CASE(dispatching_with_subcommand_match_handler) {
    deco::cli::SubCommander subcommander("catter [OPTIONS]");
    std::string seen;
    subcommander.add(
        deco::decl::SubCommand{
            .name = "run",
            .description = "Run task",
        },
        [&](const deco::cli::SubCommandMatch& match) {
            seen = std::string(match.command) + ":" + std::string(match.args().front());
        });

    std::vector<std::string> args = {"run", "-v", "--dry"};
    subcommander(args);
    EXPECT_TRUE(seen == "run:-v");
}

TEST_CASE(dispatching_with_subcommand_command) {
    std::stringstream ss;
    auto web_command = deco::cli::command<WebCliOpt>("web [OPTIONS]");
    web_command
        .match(WebCliOpt::Cate::request_category,
               [&](WebCliOpt opt) {
                   EXPECT_TRUE(opt.request.method.has_value());
                   EXPECT_TRUE(opt.request.url.has_value());
                   ss << "request-ok";
               })
        .on_error([&](auto err) { ss << "dispatch-err:" << err.message; });

    deco::cli::SubCommander subcommander("catter [OPTIONS]");
    subcommander
        .add(
            deco::decl::SubCommand{
                .name = "web",
                .description = "Web request",
            },
            web_command)
        .when_err([&](auto err) { ss << "sub-err:" << err.message; });

    std::vector<std::string> args = {"web", "-X", "GET", "--url", "https://example.com"};
    subcommander(args);
    EXPECT_TRUE(ss.str() == "request-ok");
}

TEST_CASE(usage_with_default_and_overview) {
    deco::cli::SubCommander subcommander("catter [OPTIONS]", "Catter command line");
    subcommander
        .add(
            deco::decl::SubCommand{
                .name = "run",
                .description = "Run a task",
            },
            [](std::span<std::string>) {})
        .add(
            deco::decl::SubCommand{
                .name = "inspect",
                .description = "Inspect metadata",
                .command = "show",
            },
            [](std::span<std::string>) {})
        .add([](std::span<std::string>) {});

    std::stringstream ss;
    subcommander.usage(ss);
    const auto usage = ss.str();
    EXPECT_TRUE(usage.starts_with("Catter command line"));
    EXPECT_TRUE(usage.contains("usage: catter [OPTIONS]"));
    EXPECT_TRUE(usage.contains("Subcommands:"));
    EXPECT_TRUE(usage.contains("run"));
    EXPECT_TRUE(usage.contains("Run a task"));
    EXPECT_TRUE(usage.contains("inspect"));
    EXPECT_TRUE(usage.contains("(show)"));
}

TEST_CASE(usage_without_default_and_unknown_subcommand) {
    deco::cli::SubCommander subcommander("catter [OPTIONS]", "Overview text");
    std::stringstream ss;
    subcommander
        .add(
            deco::decl::SubCommand{
                .name = "run",
                .description = "Run a task",
            },
            [](std::span<std::string>) {})
        .when_err([&](auto err) { ss << err.message; });

    std::vector<std::string> args = {"unknown"};
    subcommander(args);
    EXPECT_TRUE(ss.str().contains("unknown subcommand 'unknown'"));
    EXPECT_TRUE(ss.str().contains("at argv[0]:"));
    EXPECT_TRUE(ss.str().contains("^"));

    ss.str("");
    ss.clear();
    subcommander.usage(ss);
    const auto usage = ss.str();
    EXPECT_TRUE(usage.starts_with("Overview text"));
    EXPECT_TRUE(!usage.contains("usage: catter [OPTIONS]"));
    EXPECT_TRUE(usage.contains("Subcommands:"));
    EXPECT_TRUE(usage.contains("run"));
}

TEST_CASE(subcommand_can_use_custom_renderer) {
    std::string seen_error;
    deco::cli::SubCommander subcommander("catter [OPTIONS]", "Overview text");
    subcommander
        .add(
            deco::decl::SubCommand{
                .name = "run",
                .description = "Run a task",
            },
            [](std::span<std::string>) {})
        .render_with(make_custom_renderer())
        .when_err([&](auto err) { seen_error = err.message; });

    std::stringstream ss;
    subcommander.usage(ss);
    EXPECT_TRUE(ss.str() == "SUB<catter [OPTIONS]:1>");

    subcommander(into_deco_args("unknown"));
    EXPECT_TRUE(seen_error == "ERR<0:unknown subcommand 'unknown'>");
}

};  // TEST_SUITE(subcommander)

struct CatterOpt {
    DECO_CFG_START(required = false)

    DecoFlag(names = {"-v"})
    verbose;

    DecoInput(help = "the internal script name, like script::cdb")
    <std::string> internal_script;

    struct ScriptPath {
        std::filesystem::path path;

        std::optional<std::string> into(std::string_view input,
                                        const deco::decl::IntoContext& ctx) {
            namespace fs = std::filesystem;
            std::error_code ec;

            path = input;
            if(!fs::exists(path, ec)) {
                if(ec) {
                    return ctx.format_error(std::format("filesystem error: {}", ec.message()));
                }
                return ctx.format_error("the path does not exist!");
            }

            if(fs::is_directory(path, ec)) {
                return ctx.format_error("a file is needed");
            }
            if(ec) {
                return ctx.format_error(std::format("filesystem error: {}", ec.message()));
            }

            if(fs::is_regular_file(path, ec)) {
                return std::nullopt;
            }
            if(ec) {
                return ctx.format_error(std::format("filesystem error: {}", ec.message()));
            }

            return ctx.format_error("unsupported script path");
        }
    };

    DecoKV(names = {"-s"}, help = "the path of a catter script")
    <ScriptPath> external_script;

    DecoFlag(names = {"-h", "--help"}, help = "the path of a catter script")
    help;

    DECO_CFG_END()

    DecoPack(help = "the command args, like make, it must be after the '--'")
    <std::vector<std::string>> command;

    std::vector<std::string> script_args;
};

TEST_SUITE(deco_cases_from_user) {

TEST_CASE(catter_v2) {
    ScopedDefaultRenderer restore;
    deco::cli::text::set_default_renderer(deco::cli::text::ModernRenderer());
    auto cli = deco::cli::command<CatterOpt>(
        "catter [OPTIONS] [OPTIONS for script] -- [OPTIONS for command]");
    auto eat_script_args = [](auto& step) {
        unsigned idx = step.next_cursor();
        std::span<std::string> original_argv = step.original_argv();
        while(idx < original_argv.size() && original_argv[idx] != "--") {
            step.options().script_args.push_back(original_argv[idx++]);
        }
        return step.seek(idx);
    };
    cli.after<&CatterOpt::external_script>(eat_script_args)
        .after<&CatterOpt::internal_script>(eat_script_args)
        .after<&CatterOpt::help>([](auto& step) {
            step.usage(std::cerr);
            return step.stop();
        });

    const auto script_path =
        std::filesystem::temp_directory_path() / "eventide-catter-v2-script.tmp";
    {
        std::ofstream out(script_path);
        out << "print('hello')\n";
    }

    auto res = cli.invoke(
        into_deco_args("-s", script_path.string(), "--flag", "demo", "--", "make", "test"));
    EXPECT_TRUE(res.has_value());
    if(!res.has_value()) {
        std::filesystem::remove(script_path);
        return;
    }

    EXPECT_TRUE(res->options.external_script.has_value());
    EXPECT_TRUE(res->options.script_args == std::vector<std::string>{"--flag", "demo"});
    EXPECT_TRUE(res->options.command.has_value());
    EXPECT_TRUE(*res->options.command == std::vector<std::string>{"make", "test"});
    std::filesystem::remove(script_path);
}

};  // TEST_SUITE(deco_cases_from_user)
