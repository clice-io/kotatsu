#if defined(_MSC_VER)
// Test fixtures hold thread_local members inside anonymous-namespace structs;
// MSVC emits C5046 for the synthesized thread-local destructor registration.
#pragma warning(disable : 5046)
#endif

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "kota/deco/deco.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"

namespace kota::deco {
namespace {

namespace option = ::kota::option;

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
        constexpr static decl::Category version_category{
            .exclusive = true,
            .required = false,
            .name = "version",
            .description = "version-only mode",
        };
        constexpr static decl::Category help_category{
            .exclusive = true,
            .required = false,
            .name = "help",
            .description = "help-only mode",
        };
        constexpr static decl::Category request_category{
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

enum class BuiltinCliMode {
    Fast,
    Slow,
    Debug,
};

enum class BuiltinCliSpelledMode {
    myValue,
    Delete_,
    V123,
};

struct BuiltinEnumCliOpt {
    DecoKV(names = {"--mode"}, required = true)
    <BuiltinCliMode> mode;
};

struct BuiltinSpelledEnumCliOpt {
    DecoKV(names = {"--mode"}, required = true)
    <BuiltinCliSpelledMode> mode;
};

struct InputAndTrailingOpt {
    struct Cate {
        constexpr static decl::Category input_category{
            .exclusive = false,
            .required = false,
            .name = "input",
            .description = "single positional input",
        };
        constexpr static decl::Category trailing_category{
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
    inline thread_local static std::uint32_t arg_index = 0;
    inline thread_local static std::uint32_t next_cursor = 0;
    inline thread_local static std::size_t argv_size = 0;
    inline thread_local static std::string value;

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
    inline thread_local static std::uint32_t arg_index = 0;
    inline thread_local static std::uint32_t next_cursor = 0;
    inline thread_local static std::string value;

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
    inline thread_local static std::uint32_t arg_index = 0;
    inline thread_local static std::uint32_t next_cursor = 0;
    inline thread_local static std::string value;

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
    inline thread_local static std::uint32_t restart_count = 0;

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
    inline thread_local static std::uint32_t arg_index = 0;
    inline thread_local static std::uint32_t next_cursor = 0;
    inline thread_local static std::size_t argv_size = 0;
    inline thread_local static bool value = false;
    inline thread_local static std::uint32_t count = 0;

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

template <typename... Args>
std::span<std::string> into_deco_args(Args&&... args) {
    static thread_local std::vector<std::string> res;
    res.clear();
    res.reserve(sizeof...(args));
    (res.emplace_back(std::forward<Args>(args)), ...);
    return res;
}

struct ScopedDefaultRenderer {
    const cli::text::Renderer* saved = cli::text::explicit_default_renderer();
    std::optional<cli::text::Renderer> saved_copy =
        saved != nullptr ? std::optional<cli::text::Renderer>(*saved) : std::nullopt;

    ~ScopedDefaultRenderer() {
        if(saved_copy.has_value()) {
            cli::text::set_default_renderer(*saved_copy);
        } else {
            cli::text::clear_default_renderer();
        }
    }
};

struct ScopedDecoConfig {
    config::Config saved = config::get();

    ~ScopedDecoConfig() {
        config::set(saved);
    }
};

auto make_custom_renderer() -> cli::text::Renderer {
    auto renderer = cli::text::CompatibleRenderer();
    renderer.usage = [](const cli::text::UsageDocument& document,
                        bool include_help,
                        const cli::text::TextStyle&) {
        return std::format("USAGE<{}:{}>", document.overview, include_help ? "help" : "plain");
    };
    renderer.subcommand = [](const cli::text::SubCommandDocument& document,
                             const cli::text::TextStyle&) {
        return std::format("SUB<{}:{}>", document.usage_line, document.entries.size());
    };
    renderer.diagnostic = [](const cli::text::Diagnostic& diagnostic, const cli::text::TextStyle&) {
        return std::format("ERR<{}:{}>", diagnostic.begin, diagnostic.message);
    };
    return renderer;
}

struct CatterSelf {
    DecoFlag(required = false)
    v;
    DecoInput(required = false)
    <std::string> script_internal;
    DecoKV(required = false)
    <std::string> s;
};

auto runtime_alias_forward_pair(const deco::ParsedArgOwning& arg)
    -> std::expected<std::vector<std::string>, std::string> {
    if(arg.values.empty()) {
        return std::unexpected(std::string("missing alias payload"));
    }
    return std::vector<std::string>{"--target", arg.values.front()};
}

auto runtime_alias_forward_pair_with_context(const deco::ParsedArgOwning&,
                                             const decl::IntoContext& context)
    -> std::expected<std::vector<std::string>, std::string> {
    return std::unexpected(context.format_error("ctx failure"));
}

struct AliasRuntimeOpt {
    DecoFlag(names = {"-v"}; required = false)
    verbose;

    DecoKV(names = {"--optimize"}; required = false)
    <std::string> optimize;

    DecoKV(names = {"--target"}; required = false)
    <std::string> target;

    DecoComma(names = {"--tags"}; required = false)
    <std::vector<std::string>> tags;

    DecoMulti(2, names = {"--pair"}; required = false)
    <std::vector<std::string>> pair;

    DecoFlagAlias(names = {"-O1"}; required = false; forward = {"--optimize", "1"};) _;

    DecoKVAlias(names = {"--target-alias"}; required = false;
                forward = runtime_alias_forward_pair;) __;

    DecoCommaAlias(names = {"--tags-alias"}; required = false; forward = {"--tags"};) ___;

    DecoMultiAlias(2, names = {"--pair-alias"}; required = false; forward = {"--pair"};) ____;

    DecoFlagAlias(names = {"--ctx-fail"}; required = false;
                  forward = runtime_alias_forward_pair_with_context;) _____;
};

struct CatterTrailing {
    DecoInput(required = false)
    <std::vector<std::string>> script_args;
    DecoPack(required = false)
    <std::vector<std::string>> cmd;
};

ZEST_SUITE(deco_runtime_cli_parse) {

ZEST_CASE(parsing) {
    auto args = into_deco_args("-X", "POST", "--url", "https://example.com");
    auto res = cli::parse<WebCliOpt>(args);
    EXPECT(res);

    const auto& opt = res->options;
    EXPECT(opt.request.method->type == Request::RequestType::Type::Post);
    EXPECT(opt.request.url->url == "https://example.com");
}

ZEST_CASE(parse_result_exposes_options) {
    auto args = into_deco_args("-X", "POST", "--url", "https://example.com");
    auto res = cli::parse<WebCliOpt>(args);
    EXPECT(res);
    if(!res.has_value()) {
        return;
    }

    EXPECT(res->options.request.method->type == Request::RequestType::Type::Post);
    EXPECT(res->options.request.url->url == "https://example.com");
}

ZEST_CASE(parsing_builtin_enum) {
    auto res = cli::parse<BuiltinEnumCliOpt>(into_deco_args("--mode", "Debug"));
    EXPECT(res);
    if(!res.has_value()) {
        return;
    }

    EXPECT(res->options.mode);
    EXPECT(res->options.mode.value() == BuiltinCliMode::Debug);
}

ZEST_CASE(parsing_builtin_enum_with_serde_spelling) {
    auto snake = cli::parse<BuiltinSpelledEnumCliOpt>(into_deco_args("--mode", "my_value"));
    EXPECT(snake);
    if(!snake.has_value()) {
        return;
    }
    EXPECT(snake->options.mode.value() == BuiltinCliSpelledMode::myValue);

    auto keyword = cli::parse<BuiltinSpelledEnumCliOpt>(into_deco_args("--mode", "Delete"));
    EXPECT(keyword);
    if(!keyword.has_value()) {
        return;
    }
    EXPECT(keyword->options.mode.value() == BuiltinCliSpelledMode::Delete_);

    auto numeric = cli::parse<BuiltinSpelledEnumCliOpt>(into_deco_args("--mode", "123"));
    EXPECT(numeric);
    if(!numeric.has_value()) {
        return;
    }
    EXPECT(numeric->options.mode.value() == BuiltinCliSpelledMode::V123);
}

ZEST_CASE(parsing_input_and_trailing) {
    auto args = into_deco_args("front", "--", "a", "b", "c");
    auto res = cli::parse<InputAndTrailingOpt>(args);
    EXPECT(res);
    if(!res.has_value()) {
        return;
    }
    const auto& opt = res->options;
    EXPECT(*opt.input == "front");
    EXPECT(opt.trailing->size() == 3);
    EXPECT((*opt.trailing)[0] == "a");
    EXPECT((*opt.trailing)[1] == "b");
    EXPECT((*opt.trailing)[2] == "c");
    EXPECT(res->matched_categories.contains(&InputAndTrailingOpt::Cate::input_category));
    EXPECT(res->matched_categories.contains(&InputAndTrailingOpt::Cate::trailing_category));
}

ZEST_CASE(parsing_trailing_requires_dash_dash_separator) {
    auto bad = cli::parse<TrailingOnlyOpt>(into_deco_args("front"));
    EXPECT(!bad);
    EXPECT(bad.error().type == cli::ParseError::Type::DecoParsing);
    EXPECT(bad.error().message.contains("unexpected input argument"));

    auto good = cli::parse<TrailingOnlyOpt>(into_deco_args("--", "a", "b"));
    EXPECT(good);
    if(!good.has_value()) {
        return;
    }
    EXPECT(good->options.trailing);
    EXPECT(good->options.trailing->size() == 2);
    EXPECT((*good->options.trailing)[0] == "a");
    EXPECT((*good->options.trailing)[1] == "b");
}

ZEST_CASE(when_error) {
    auto res = cli::parse<WebCliOpt>(into_deco_args("-X", "INVALID"));
    EXPECT(!res);
    EXPECT((res.error().type == cli::ParseError::Type::IntoError &&
            res.error().message.contains("Invalid request type")));

    auto res2 = cli::parse<WebCliOpt>(into_deco_args("--url", "ftp://example.com"));
    EXPECT(!res2);
    EXPECT((res2.error().type == cli::ParseError::Type::IntoError &&
            res2.error().message.contains("Invalid URL")));

    auto res3 = cli::parse<WebCliOpt>(into_deco_args("--unknown"));
    EXPECT(!res3);
    EXPECT((res3.error().type == cli::ParseError::Type::BackendParsing &&
            res3.error().message.contains("unknown option")));

    auto res4 = cli::parse<WebCliOpt>(into_deco_args("-v", "--help"));
    EXPECT(!res4);
    EXPECT((res4.error().type == cli::ParseError::Type::DecoParsing &&
            res4.error().message.contains("exclusive")));

    auto res5 = cli::parse<WebCliOpt>(into_deco_args("-X", "GET"));
    EXPECT(!res5);
    EXPECT((res5.error().type == cli::ParseError::Type::DecoParsing &&
            res5.error().message.contains("required option")));

    auto res6 = cli::parse<WebCliOpt>(into_deco_args("--", "a", "b"));
    EXPECT(!res6);
    EXPECT((res6.error().type == cli::ParseError::Type::BackendParsing &&
            res6.error().message.contains("unknown option")));

    auto res7 = cli::parse<BuiltinEnumCliOpt>(into_deco_args("--mode", "Turbo"));
    EXPECT(!res7);
    EXPECT(res7.error().type == cli::ParseError::Type::IntoError);
    EXPECT(res7.error().message.contains("invalid enum value: Turbo"));
    EXPECT(res7.error().message.contains("supported: fast, slow, debug"));

    auto res8 = cli::parse<BuiltinSpelledEnumCliOpt>(into_deco_args("--mode", "nope"));
    EXPECT(!res8);
    EXPECT(res8.error().type == cli::ParseError::Type::IntoError);
    EXPECT(res8.error().message.contains("invalid enum value: nope"));
    EXPECT(res8.error().message.contains("supported: myValue, delete, v123"));
}

ZEST_CASE(parse_errors_include_location_context) {
    auto res = cli::parse<WebCliOpt>(into_deco_args("--unknown"));
    EXPECT(!res);
    if(res.has_value()) {
        return;
    }

    EXPECT(res.error().message.contains("at argv[0]:"));
    EXPECT(res.error().message.contains("--unknown"));
    EXPECT(res.error().message.contains("^"));

    auto enum_res = cli::parse<BuiltinEnumCliOpt>(into_deco_args("--mode", "Turbo"));
    EXPECT(!enum_res);
    if(enum_res.has_value()) {
        return;
    }

    EXPECT(enum_res.error().message.contains("at argv[1]:"));
    EXPECT(enum_res.error().message.contains("Turbo"));
    EXPECT(enum_res.error().message.contains("supported: fast, slow, debug"));
}

ZEST_CASE(global_compatible_renderer_config_can_disable_positioned_diagnostics) {
    ScopedDecoConfig restore;
    auto updated = config::get();
    updated.render.compatible.diagnostic.enabled = false;
    config::set(updated);

    auto res = cli::parse<WebCliOpt>(into_deco_args("--unknown"));
    EXPECT(!res);
    if(res.has_value()) {
        return;
    }

    EXPECT(!res.error().message.contains("at argv["));
    EXPECT(!res.error().message.contains("^"));
    EXPECT(res.error().message == "unknown option '--unknown'");
}

ZEST_CASE(parse_overload_accepts_custom_renderer) {
    auto renderer = make_custom_renderer();
    auto res = cli::parse<WebCliOpt>(into_deco_args("--unknown"), renderer);
    EXPECT(!res);
    if(res.has_value()) {
        return;
    }

    EXPECT(res.error().message == "ERR<0:unknown option '--unknown'>");
}

ZEST_CASE(diagnostic_at_uses_non_owning_argv_view) {
    auto argv = into_deco_args("--unknown", "value");
    const auto argv_view = std::span<const std::string>(argv.data(), argv.size());
    auto diagnostic = cli::text::diagnostic_at(argv_view, 0, 1, "boom");

    EXPECT(diagnostic.argv.data() == argv.data());
    argv[0] = "--renamed";

    auto renderer = cli::text::CompatibleRenderer();
    const auto rendered = cli::text::render_diagnostic(diagnostic, &renderer);
    EXPECT(rendered.contains("--renamed"));
}

ZEST_CASE(modern_renderer_highlights_usage_and_diagnostic) {
    auto renderer = cli::text::ModernRenderer();
    const auto usage_document = cli::text::UsageDocument{
        .overview = "webcli [OPTIONS]",
        .groups =
            {
                     cli::text::UsageGroup{
                    .title = "network",
                    .entries =
                        {
                            cli::text::UsageEntry{
                                .usage = "--host <value>",
                                .help = "connect target",
                            },
                        },
                }, },
    };

    const auto usage = cli::text::render_usage(usage_document, true, &renderer);
    EXPECT(usage.contains("\033["));
    EXPECT(usage.contains("Usage"));
    EXPECT(usage.contains("Options"));
    EXPECT(usage.contains("\033[39mconnect target\033[0m"));
    EXPECT(usage.contains("\033[1;4;38;5;110mnetwork\033[0m"));

    auto argv = into_deco_args("webcli", "--unknown");
    const auto diagnostic = cli::text::render_diagnostic(
        cli::text::diagnostic_at(std::span<const std::string>(argv.data(), argv.size()),
                                 1,
                                 2,
                                 "boom"),
        &renderer);
    EXPECT(diagnostic.contains("\033["));
    EXPECT(diagnostic.contains("╰─▶"));
    EXPECT(diagnostic.contains("\033[39m[argv[1]]\033[0m"));
}

ZEST_CASE(modern_renderer_crops_long_diagnostic_source_line) {
    auto renderer = cli::text::ModernRenderer();

    const std::string very_long_a(200, 'a');
    const std::string very_long_b(220, 'b');
    auto argv = into_deco_args("-s", very_long_a, very_long_b, "--", "make");
    const auto diagnostic = cli::text::render_diagnostic(
        cli::text::diagnostic_at(std::span<const std::string>(argv.data(), argv.size()),
                                 1,
                                 2,
                                 "too long"),
        &renderer);

    EXPECT(diagnostic.contains("..."));
    EXPECT(diagnostic.contains("╰─▶"));
    EXPECT(diagnostic.contains("too long"));
    EXPECT(!diagnostic.contains(very_long_b));
}

ZEST_CASE(with_cont_parse) {
    std::vector<std::string> args = {"-v", "script::cdb", "-t", "x", "--", "make"};
    auto res = cli::parse_with_callback<CatterSelf>(
        args,
        [](const CatterSelf& opt, decl::DecoOptionBase* ptr) {
            return !(&opt.s == ptr || &opt.script_internal == ptr);
        });
    auto res2 = cli::parse<CatterTrailing>({args.begin() + res->next_index, args.end()});
    EXPECT(res->next_index == 2);
    EXPECT(*res->options.script_internal == "script::cdb");
    EXPECT(res2->options.cmd->size() == 1);
    EXPECT((*res2->options.script_args)[0] == "-t");
}

ZEST_CASE(invocation_exposes_trace_and_remaining_args) {
    std::vector<std::string> args = {"-v", "script::cdb", "-t", "x", "--", "make"};
    auto command = cli::command<CatterSelf>("catter");
    command.after<&CatterSelf::script_internal>([](const auto& step) { return step.stop(); });
    auto res = command.invoke(args);
    EXPECT(res);
    if(!res.has_value()) {
        return;
    }

    EXPECT(res->next_cursor() == 2u);
    EXPECT(res->argv().size() == 6u);
    EXPECT(res->remaining().size() == 4u);
    EXPECT(res->remaining()[0] == "-t");
    EXPECT(res->trace().size() == 2u);
    EXPECT(res->trace()[0].spelling == "-v");
    EXPECT(res->trace()[1].spelling == "script::cdb");
}

ZEST_CASE(option_callback_can_stop_early_with_current_result) {
    CallbackStopState::reset();

    auto res = cli::parse<CallbackStopOpt>(into_deco_args("script.lua"));
    EXPECT(res);
    if(!res.has_value()) {
        return;
    }

    EXPECT(res->options.script);
    EXPECT(*res->options.script == "script.lua");
    EXPECT(!res->options.required_after_stop);
    EXPECT(res->next_index == 1);
    EXPECT(CallbackStopState::arg_index == 0u);
    EXPECT(CallbackStopState::next_cursor == 1u);
    EXPECT(CallbackStopState::argv_size == 1u);
    EXPECT(CallbackStopState::value == "script.lua");
}

ZEST_CASE(option_callback_can_restart_with_new_span) {
    CallbackRestartState::reset();

    auto res =
        cli::parse<CallbackRestartOpt>(into_deco_args("entry.cc", "--skip", "ignored", "-v"));
    EXPECT(res);
    if(!res.has_value()) {
        return;
    }

    EXPECT(res->options.script);
    EXPECT(*res->options.script == "entry.cc");
    EXPECT(!res->options.skip);
    EXPECT((res->options.verbose.has_value() && *res->options.verbose));
    EXPECT(res->next_index == 1);
    EXPECT(CallbackRestartState::arg_index == 0u);
    EXPECT(CallbackRestartState::next_cursor == 1u);
    EXPECT(CallbackRestartState::value == "entry.cc");
}

ZEST_CASE(option_callback_can_restart_with_owned_argv) {
    CallbackRestartOwnedState::reset();

    auto res = cli::parse<CallbackRestartOwnedOpt>(into_deco_args("entry.cc"));
    EXPECT(res);
    if(!res.has_value()) {
        return;
    }

    EXPECT(res->options.script);
    EXPECT(*res->options.script == "entry.cc");
    EXPECT((res->options.verbose.has_value() && *res->options.verbose));
    EXPECT(res->argv().size() == 1u);
    EXPECT(res->argv()[0] == "-v");
    EXPECT(res->next_index == 1);
    EXPECT(CallbackRestartOwnedState::arg_index == 0u);
    EXPECT(CallbackRestartOwnedState::next_cursor == 1u);
    EXPECT(CallbackRestartOwnedState::value == "entry.cc");
}

ZEST_CASE(alias_can_forward_and_restart_without_replaying_prefix) {
    auto res = cli::parse<AliasRuntimeOpt>(into_deco_args("-v", "-O1", "--target-alias", "dst"));
    EXPECT(res);
    if(!res.has_value()) {
        return;
    }

    EXPECT((res->options.verbose.has_value() && *res->options.verbose));
    EXPECT(res->options.optimize);
    EXPECT(*res->options.optimize == "1");
    EXPECT(res->options.target);
    EXPECT(*res->options.target == "dst");
    EXPECT(res->next_index == 2u);
}

ZEST_CASE(alias_static_forward_supports_comma_and_multi_shapes) {
    auto res = cli::parse<AliasRuntimeOpt>(
        into_deco_args("--tags-alias,a,b", "--pair-alias", "left", "right"));
    EXPECT(res);
    if(!res.has_value()) {
        return;
    }

    EXPECT(res->options.tags);
    EXPECT(res->options.tags->size() == 2);
    EXPECT((*res->options.tags)[0] == "a");
    EXPECT((*res->options.tags)[1] == "b");
    EXPECT(res->options.pair);
    EXPECT(res->options.pair->size() == 2);
    EXPECT((*res->options.pair)[0] == "left");
    EXPECT((*res->options.pair)[1] == "right");
}

ZEST_CASE(alias_dynamic_with_context_preserves_preformatted_errors) {
    auto res = cli::parse<AliasRuntimeOpt>(into_deco_args("--ctx-fail"));
    EXPECT(!res);
    if(res.has_value()) {
        return;
    }

    EXPECT(res.error().type == cli::ParseError::Type::IntoError);
    EXPECT(res.error().message.contains("ctx failure"));
    EXPECT(res.error().message.find("at argv[0]:") == 0);
    EXPECT(res.error().message.find("at argv[0]:", 1) == std::string::npos);
}

ZEST_CASE(schema_only_alias_usage_does_not_expose_generated_wrapper_names) {
    auto usage = cli::text::render_usage(
        cli::detail::make_usage_document<AliasRuntimeOpt>("alias [OPTIONS]"),
        true,
        nullptr);

    EXPECT(usage.contains("-O1"));
    EXPECT(usage.contains("--target-alias"));
    EXPECT(usage.contains("--tags-alias"));
    EXPECT(usage.contains("--pair-alias"));
    EXPECT(!usage.contains("__deco_alias_wrapper"));
    EXPECT(!usage.contains("--__deco_alias_wrapper"));
}

ZEST_CASE(option_callback_can_restart_multiple_times_with_owned_argv) {
    CallbackRestartTwiceState::reset();

    auto res = cli::parse<CallbackRestartTwiceOpt>(into_deco_args("first.cc"));
    EXPECT(res);
    if(!res.has_value()) {
        return;
    }

    EXPECT(CallbackRestartTwiceState::restart_count == 2u);
    EXPECT(res->options.script);
    EXPECT(*res->options.script == "second.cc");
    EXPECT(res->options.name);
    EXPECT(*res->options.name == "final");
    EXPECT(res->argv().size() == 2u);
    EXPECT(res->argv()[0] == "--name");
    EXPECT(res->argv()[1] == "final");
}

ZEST_CASE(option_callback_supports_action_shortcut) {
    auto res = cli::parse<CallbackShortcutOpt>(into_deco_args("shortcut.lua"));
    EXPECT(res);
    if(!res.has_value()) {
        return;
    }

    EXPECT(res->options.script);
    EXPECT(*res->options.script == "shortcut.lua");
    EXPECT(!res->options.required_after_stop);
    EXPECT(res->next_index == 1);
}

ZEST_CASE(command_after_runs_after_field_callback) {
    CallbackComposeState::reset();
    std::uint32_t command_count = 0;

    auto command = cli::command<CallbackComposeOpt>("compose");
    command.after<&CallbackComposeOpt::verbose>([&](const auto& step) {
        EXPECT(CallbackComposeState::count == 1u);
        EXPECT(step.value());
        ++command_count;
        return step.stop();
    });

    auto res = command.invoke(into_deco_args("-v", "--rest", "tail"));
    EXPECT(res);
    if(!res.has_value()) {
        return;
    }

    EXPECT((res->options.verbose.has_value() && *res->options.verbose));
    EXPECT(!res->options.rest);
    EXPECT(res->next_index == 1);
    EXPECT(CallbackComposeState::count == 1u);
    EXPECT(command_count == 1u);
    EXPECT(CallbackComposeState::arg_index == 0u);
    EXPECT(CallbackComposeState::next_cursor == 1u);
    EXPECT(CallbackComposeState::argv_size == 3u);
    EXPECT(CallbackComposeState::value);
}

ZEST_CASE(command_after_supports_nested_member_paths) {
    std::uint32_t hit_count = 0;
    std::string seen;

    auto command = cli::command<NestedAfterOpt>("nested");
    command
        .after<&NestedAfterOpt::first, &NestedAfterFirstBranch::leaf, &NestedAfterFirstLeaf::token>(
            [&](const auto& step) {
                ++hit_count;
                seen = step.value();
                EXPECT(step.arg().spelling == "--first-token");
                return step.next();
            });

    auto res = command.invoke(into_deco_args("--second-token", "other", "--first-token", "hit"));
    EXPECT(res);
    if(!res.has_value()) {
        return;
    }

    EXPECT(res->options.first.leaf.token);
    EXPECT(res->options.second.leaf.token);
    EXPECT(*res->options.first.leaf.token == "hit");
    EXPECT(*res->options.second.leaf.token == "other");
    EXPECT(hit_count == 1u);
    EXPECT(seen == "hit");
}

ZEST_CASE(command_callbacks_can_capture_state_finalize_and_match_all) {
    std::string seen;
    std::string entry;
    auto command = cli::command<CommandFlowOpt>("run");
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
    EXPECT(seen == "entry:main.lua|default");
}

};  // ZEST_SUITE(deco_runtime_cli_parse)

ZEST_SUITE(deco_runtime_command_match) {

ZEST_CASE(match_dispatches_by_category) {
    auto command = cli::command<WebCliOpt>("webcli [OPTIONS]");
    std::stringstream ss;
    command.match(WebCliOpt::Cate::version_category, [&](auto) { ss << "Version 1.0.0"; })
        .match(WebCliOpt::Cate::help_category, [&](auto) { command.usage(ss, true); })
        .match(WebCliOpt::Cate::request_category,
               [&](WebCliOpt opt) {
                   EXPECT(opt.request.method);
                   EXPECT(opt.request.url);
               })
        .on_error([&](auto err) { ss << "Error: " << err.message << "\n"; });

    command(into_deco_args("-v"));
    EXPECT(ss.str().contains("Version 1.0.0"));

    ss.str("");
    command(into_deco_args("--help"));
    EXPECT(ss.str().contains("webcli [OPTIONS]"));

    ss.str("");
    command(into_deco_args("-X", "GET", "--url", "https://example.com"));
}

ZEST_CASE(match_can_observe_invocation_context) {
    auto command = cli::command<WebCliOpt>("webcli [OPTIONS]");
    std::string seen_url;
    std::uint32_t seen_trace_size = 0;
    command.match(WebCliOpt::Cate::request_category,
                  [&](const cli::Invocation<WebCliOpt>& invocation) {
                      EXPECT(invocation.matched(WebCliOpt::Cate::request_category));
                      seen_trace_size = static_cast<std::uint32_t>(invocation.trace().size());
                      seen_url = invocation.options.request.url->url;
                  });

    command(into_deco_args("-X", "GET", "--url", "https://example.com"));
    EXPECT(seen_url == "https://example.com");
    EXPECT(seen_trace_size == 2u);
}

ZEST_CASE(command_can_use_compatible_renderer_config) {
    auto command = cli::command<WebCliOpt>("webcli [OPTIONS]");
    cli::text::CompatibleRendererConfig config{};
    config.usage.options_heading = "Flags:";
    config.usage.group_by_category = false;
    command.render_with_compatible(config);

    std::stringstream ss;
    command.usage(ss);
    EXPECT(ss.str().contains("Flags:"));
    EXPECT(!ss.str().contains("Options:"));
}

ZEST_CASE(command_can_use_custom_renderer) {
    std::string seen_error;
    auto command = cli::command<WebCliOpt>("webcli [OPTIONS]");
    command.render_with(make_custom_renderer()).on_error([&](auto err) {
        seen_error = err.message;
    });

    std::stringstream ss;
    command.usage(ss);
    EXPECT(ss.str() == "USAGE<webcli [OPTIONS]:help>");

    command(into_deco_args("--unknown"));
    EXPECT(seen_error == "ERR<0:unknown option '--unknown'>");
}

ZEST_CASE(command_default_renderer_overrides_config_fallback) {
    ScopedDefaultRenderer restore_renderer;
    ScopedDecoConfig restore_config;

    auto updated = config::get();
    updated.render.compatible.usage.options_heading = "Flags:";
    config::set(updated);
    cli::text::set_default_renderer(cli::text::ModernRenderer());

    auto command = cli::command<WebCliOpt>("webcli [OPTIONS]");
    std::stringstream ss;
    command.usage(ss);

    EXPECT(ss.str().contains("Usage"));
    EXPECT(!ss.str().contains("Flags:"));
}

ZEST_CASE(command_explicit_renderer_overrides_default_renderer) {
    ScopedDefaultRenderer restore;
    cli::text::set_default_renderer(cli::text::ModernRenderer());

    auto command = cli::command<WebCliOpt>("webcli [OPTIONS]");
    command.render_with(make_custom_renderer());

    std::stringstream ss;
    command.usage(ss);
    EXPECT(ss.str() == "USAGE<webcli [OPTIONS]:help>");
}

};  // ZEST_SUITE(deco_runtime_command_match)

ZEST_SUITE(deco_runtime_subcommander) {

ZEST_CASE(dispatching_with_subcommand_and_default) {
    cli::SubCommander subcommander("catter [OPTIONS]");
    std::stringstream ss;
    subcommander
        .add(
            decl::SubCommand{
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
    EXPECT(ss.str() == "run:-v,--dry,");

    ss.str("");
    ss.clear();

    std::vector<std::string> default_args = {"--help"};
    subcommander(default_args);
    EXPECT(ss.str() == "default:--help,");
}

ZEST_CASE(match_reports_subcommand_context) {
    cli::SubCommander subcommander("catter [OPTIONS]");
    subcommander.add(
        decl::SubCommand{
            .name = "run",
            .description = "Run task",
        },
        [](std::span<std::string>) {});

    auto match = subcommander.match(into_deco_args("run", "-v", "--dry"));
    EXPECT(match);
    if(!match.has_value()) {
        return;
    }

    EXPECT(match->is_command());
    EXPECT(match->command == "run");
    EXPECT(match->name == "run");
    EXPECT(match->args().size() == 2u);
    EXPECT(match->args()[0] == "-v");
}

ZEST_CASE(dispatching_with_subcommand_match_handler) {
    cli::SubCommander subcommander("catter [OPTIONS]");
    std::string seen;
    subcommander.add(
        decl::SubCommand{
            .name = "run",
            .description = "Run task",
        },
        [&](const cli::SubCommandMatch& match) {
            seen = std::string(match.command) + ":" + std::string(match.args().front());
        });

    std::vector<std::string> args = {"run", "-v", "--dry"};
    subcommander(args);
    EXPECT(seen == "run:-v");
}

ZEST_CASE(dispatching_with_subcommand_command) {
    std::stringstream ss;
    auto web_command = cli::command<WebCliOpt>("web [OPTIONS]");
    web_command
        .match(WebCliOpt::Cate::request_category,
               [&](WebCliOpt opt) {
                   EXPECT(opt.request.method);
                   EXPECT(opt.request.url);
                   ss << "request-ok";
               })
        .on_error([&](auto err) { ss << "dispatch-err:" << err.message; });

    cli::SubCommander subcommander("catter [OPTIONS]");
    subcommander
        .add(
            decl::SubCommand{
                .name = "web",
                .description = "Web request",
            },
            web_command)
        .when_err([&](auto err) { ss << "sub-err:" << err.message; });

    std::vector<std::string> args = {"web", "-X", "GET", "--url", "https://example.com"};
    subcommander(args);
    EXPECT(ss.str() == "request-ok");
}

ZEST_CASE(usage_with_default_and_overview) {
    cli::SubCommander subcommander("catter [OPTIONS]", "Catter command line");
    subcommander
        .add(
            decl::SubCommand{
                .name = "run",
                .description = "Run a task",
            },
            [](std::span<std::string>) {})
        .add(
            decl::SubCommand{
                .name = "inspect",
                .description = "Inspect metadata",
                .command = "show",
            },
            [](std::span<std::string>) {})
        .add([](std::span<std::string>) {});

    std::stringstream ss;
    subcommander.usage(ss);
    const auto usage = ss.str();
    EXPECT(zest::starts_with(usage, "Catter command line"));
    EXPECT(usage.contains("usage: catter [OPTIONS]"));
    EXPECT(usage.contains("Subcommands:"));
    EXPECT(usage.contains("run"));
    EXPECT(usage.contains("Run a task"));
    EXPECT(usage.contains("inspect"));
    EXPECT(usage.contains("(show)"));
}

ZEST_CASE(usage_without_default_and_unknown_subcommand) {
    cli::SubCommander subcommander("catter [OPTIONS]", "Overview text");
    std::stringstream ss;
    subcommander
        .add(
            decl::SubCommand{
                .name = "run",
                .description = "Run a task",
            },
            [](std::span<std::string>) {})
        .when_err([&](auto err) { ss << err.message; });

    std::vector<std::string> args = {"unknown"};
    subcommander(args);
    EXPECT(ss.str().contains("unknown subcommand 'unknown'"));
    EXPECT(ss.str().contains("at argv[0]:"));
    EXPECT(ss.str().contains("^"));

    ss.str("");
    ss.clear();
    subcommander.usage(ss);
    const auto usage = ss.str();
    EXPECT(zest::starts_with(usage, "Overview text"));
    EXPECT(!usage.contains("usage: catter [OPTIONS]"));
    EXPECT(usage.contains("Subcommands:"));
    EXPECT(usage.contains("run"));
}

ZEST_CASE(subcommand_can_use_custom_renderer) {
    std::string seen_error;
    cli::SubCommander subcommander("catter [OPTIONS]", "Overview text");
    subcommander
        .add(
            decl::SubCommand{
                .name = "run",
                .description = "Run a task",
            },
            [](std::span<std::string>) {})
        .render_with(make_custom_renderer())
        .when_err([&](auto err) { seen_error = err.message; });

    std::stringstream ss;
    subcommander.usage(ss);
    EXPECT(ss.str() == "SUB<catter [OPTIONS]:1>");

    subcommander(into_deco_args("unknown"));
    EXPECT(seen_error == "ERR<0:unknown subcommand 'unknown'>");
}

ZEST_CASE(subcommand_can_use_modern_renderer_config) {
    cli::SubCommander subcommander("catter [OPTIONS]", "Overview text");
    subcommander
        .add(
            decl::SubCommand{
                .name = "run",
                .description = "Run a task",
            },
            [](std::span<std::string>) {})
        .render_with_modern();

    std::stringstream ss;
    subcommander.usage(ss);
    EXPECT(ss.str().contains("Commands"));
    EXPECT(ss.str().contains("Overview text"));
    EXPECT(ss.str().contains("run"));
}

};  // ZEST_SUITE(deco_runtime_subcommander)

struct CatterOpt {
    DECO_CFG_START(required = false)

    DecoFlag(names = {"-v"})
    verbose;

    DecoInput(help = "the internal script name, like script::cdb")
    <std::string> internal_script;

    struct ScriptPath {
        std::filesystem::path path;

        std::optional<std::string> into(std::string_view input, const decl::IntoContext& ctx) {
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

ZEST_SUITE(deco_runtime_cases_from_user) {

ZEST_CASE(catter_v2) {
    ScopedDefaultRenderer restore;
    cli::text::set_default_renderer(cli::text::ModernRenderer());
    auto cli =
        cli::command<CatterOpt>("catter [OPTIONS] [OPTIONS for script] -- [OPTIONS for command]");
    auto eat_script_args = [](auto& step) {
        std::uint32_t idx = step.next_cursor();
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
        std::filesystem::temp_directory_path() / "kotatsu-catter-v2-script.tmp";
    {
        std::ofstream out(script_path);
        out << "print('hello')\n";
    }

    auto res = cli.invoke(
        into_deco_args("-s", script_path.string(), "--flag", "demo", "--", "make", "test"));
    EXPECT(res);
    if(!res.has_value()) {
        std::filesystem::remove(script_path);
        return;
    }

    EXPECT(res->options.external_script);
    EXPECT(res->options.script_args == std::vector<std::string>{"--flag", "demo"});
    EXPECT(res->options.command);
    EXPECT(*res->options.command == std::vector<std::string>{"make", "test"});
    std::filesystem::remove(script_path);
}

};  // ZEST_SUITE(deco_runtime_cases_from_user)

}  // namespace
}  // namespace kota::deco
