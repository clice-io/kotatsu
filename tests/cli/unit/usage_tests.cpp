// The usage of a struct of options, laid out, and as entries a program lays out itself.

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "kotatsu/cli/cli.h"
#include "kotatsu/zest/zest.h"

namespace kotatsu::cli {

namespace {

enum class Mode : std::uint8_t { Pipe, Socket };

struct Where {
    KOTATSU_ANNOTATE(description = "Project root directory")
    <std::optional<std::string>> workspace;
};

struct Remote {
    KOTATSU_ANNOTATE(description = "Host to connect to")
    <std::string> host;
};

struct Serve {
    constexpr static std::string_view description = "Start the server";
    cli::Help help;
    KOTATSU_ANNOTATE(description = "Server mode")
    <Mode> mode = Mode::Pipe;
    KOTATSU_ANNOTATE(cli::names = {"-j", "--jobs"}, cli::meta_var = "<N>", description = "Workers")
    <unsigned> jobs = 0;
    KOTATSU_ANNOTATE(description = "Scopes to look in")
    <std::vector<std::string>> scope;
    std::optional<std::string> record;
    Where where;
    KOTATSU_ANNOTATE(description = "Remote")
    <std::optional<Remote>> remote;
    KOTATSU_ANNOTATE(cli::positional = true, cli::meta_var = "<PATH>", description = "Files")
    <std::vector<std::string>> paths;
};

struct Query {
    constexpr static std::string_view description = "Ask the index";
};

struct Tool {
    constexpr static std::string_view description = "A tool";
    cli::Help help;
    std::variant<Serve, Query> command;
};

struct Internal {
    KOTATSU_ANNOTATE(cli::hidden = true)
    <bool> trace = false;
    KOTATSU_ANNOTATE(cli::hidden_sub = {"serve"})
    <std::variant<Serve, Query>> command;
};

struct Run {
    KOTATSU_ANNOTATE(cli::rest = true, cli::meta_var = "<ARG>")
    <std::vector<std::string>> command;
};

ZEST_SUITE(cli_usage) {

ZEST_CASE(usage_lists_options_with_their_help) {
    ZEXPECT(cli::usage<Serve>("tool serve [OPTIONS]") ==
            "Start the server\n"
            "\n"
            "usage: tool serve [OPTIONS]\n"
            "\n"
            "Options:\n"
            "  -h|--help                       display this help and exit\n"
            "  --mode <pipe|socket>            Server mode [default: pipe]\n"
            "  -j|--jobs <N>                   Workers [default: 0]\n"
            "  --scope <value>                 Scopes to look in (may be repeated)\n"
            "  --record <value>                no description provided\n"
            "  --workspace <value>             Project root directory\n"
            "  <PATH>...                       Files\n"
            "\n"
            "Remote:\n"
            "  --host <value>                  Host to connect to\n");
}

ZEST_CASE(usage_lists_subcommands_with_their_descriptions) {
    ZEXPECT(cli::usage<Tool>("tool <command>") ==
            "A tool\n"
            "\n"
            "usage: tool <command>\n"
            "\n"
            "Options:\n"
            "  -h|--help                       display this help and exit\n"
            "\n"
            "Subcommands:\n"
            "  serve  Start the server\n"
            "  query  Ask the index\n");
}

ZEST_CASE(long_usage_puts_its_help_below) {
    const auto usage = cli::usage<Serve>("serve", Style{.help_column = 10});
    ZEXPECT(usage.contains("  --mode <pipe|socket>\n            Server mode [default: pipe]\n"));
}

ZEST_CASE(color_look_paints_headings) {
    const auto usage = cli::usage<Tool>("tool", Style{.color = true});
    ZEXPECT(usage.contains("\033[1;38;5;81mUsage\033[0m"));
    ZEXPECT(usage.contains("Commands"));
}

ZEST_CASE(entries_say_what_each_is) {
    const auto entries = cli::usage_entries<Tool>();
    ZASSERT(entries.size() == 3U);
    ZEXPECT(entries[0].kind == UsageEntry::Kind::Option);
    ZEXPECT(entries[0].usage == "-h|--help");
    ZEXPECT(entries[1].kind == UsageEntry::Kind::Subcommand);
    ZEXPECT(entries[1].usage == "serve");
    ZEXPECT(entries[1].help == "Start the server");
}

ZEST_CASE(entries_say_their_group_and_whether_they_repeat) {
    const auto serve = cli::usage_entries<Serve>();
    ZASSERT(serve.size() == 8U);
    ZEXPECT(!serve[2].repeatable);
    ZEXPECT(serve[3].repeatable);
    ZEXPECT(serve[6].usage == "--host <value>");
    ZEXPECT(serve[6].group == "Remote");
    ZEXPECT(serve[7].kind == UsageEntry::Kind::Positional);
}

ZEST_CASE(rest_is_positional_and_said_once) {
    const auto entries = cli::usage_entries<Run>();
    ZASSERT(entries.size() == 1U);
    ZEXPECT(entries[0].kind == UsageEntry::Kind::Positional);
    ZEXPECT(entries[0].usage == "-- <ARG>...");
    ZEXPECT(!entries[0].repeatable);
}

ZEST_CASE(hidden_ones_are_left_out) {
    const auto entries = cli::usage_entries<Internal>();
    ZASSERT(entries.size() == 1U);
    ZEXPECT(entries[0].usage == "query");
}

ZEST_CASE(hidden_ones_still_parse) {
    auto internal = cli::parse<Internal>(std::vector<std::string>{"--trace", "serve"});
    ZASSERT(internal.has_value());
    ZEXPECT(internal->trace);
    ZEXPECT(internal->command.index() == 0U);
}

};  // ZEST_SUITE(cli_usage)

}  // namespace

}  // namespace kotatsu::cli
