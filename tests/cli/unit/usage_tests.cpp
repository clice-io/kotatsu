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
    KANNOTATE(&workspace, description = "Project root directory")
    std::optional<std::string> workspace;
};

struct Remote {
    KANNOTATE(&host, description = "Host to connect to")
    std::string host;
};

KANNOTATE_TYPE(struct Serve, description = "Start the server");

struct Serve {
    KANNOTATE(&mode, description = "Server mode")
    Mode mode = Mode::Pipe;
    KANNOTATE(&jobs, cli::names = {"-j", "--jobs"}, cli::meta_var = "<N>", description = "Workers")
    unsigned jobs = 0;
    KANNOTATE(&scope, description = "Scopes to look in")
    std::vector<std::string> scope;
    std::optional<std::string> record;
    Where where;
    KANNOTATE(&remote, description = "Remote")
    std::optional<Remote> remote;
    KANNOTATE(&paths, cli::positional = true, cli::meta_var = "<PATH>", description = "Files")
    std::vector<std::string> paths;
};

KANNOTATE_TYPE(struct Query, description = "Ask the index");

struct Query {};

KANNOTATE_TYPE(struct Tool, description = "A tool");

struct Tool {
    std::variant<Serve, Query> command;
};

struct Internal {
    KANNOTATE(&trace, cli::hidden = true, cli::global = true)
    bool trace = false;
    KANNOTATE(&command, cli::hidden_sub = {"serve"})
    std::variant<Serve, Query> command;
};

struct Tuned {
    KANNOTATE(&jobs, minimum = 1, maximum = 64, description = "Workers")
    int jobs = 4;
    KANNOTATE(&mode, choices = {"fast", "slow"})
    std::string mode = "fast";
    std::vector<std::string> include = {"a", "b"};
    std::string name;
};

struct Must {
    KANNOTATE(&must, presence = required)
    std::string must = "x";
};

enum class Level : std::uint8_t { Low = 1, High };

/// A field whose default value has no name, which no argument writes.
struct Unnamed {
    KANNOTATE(&level, presence = required)
    Level level;
    int jobs = 2;
};

struct Run {
    KANNOTATE(&command, cli::rest = true, cli::meta_var = "<ARG>")
    std::vector<std::string> command;
};

/// Options a repr reads from fields of their own, refusing them without a host.
struct Endpoint {
    std::string host = "localhost";
};

struct EndpointFields {
    std::string host;
};

struct Connect {
    KANNOTATE(&command, tagged = true, tag_names = {"endpoint"})
    std::variant<Endpoint> command;
};

/// Commands taking one of the help's names, or both, with options of their own.
struct NamesH {
    KANNOTATE(&host, cli::names = {"-h", "--host"})
    std::string host;
};

struct SlashH {
    KANNOTATE(&host, cli::names = {"/h"})
    std::string host;
};

struct HelpField {
    bool help = false;
};

struct BothNames {
    KANNOTATE(&help, cli::names = {"-h", "--help"})
    bool help = false;
};

}  // namespace

}  // namespace kotatsu::cli

template <>
struct kotatsu::codec::repr<kotatsu::cli::Endpoint> {
    using type = kotatsu::cli::EndpointFields;

    template <typename F>
    static bool encode(auto& writer, const kotatsu::cli::Endpoint& endpoint) {
        return codec::encode_value<F>(writer, kotatsu::cli::EndpointFields{endpoint.host});
    }

    template <typename F>
    static bool decode(auto& reader, kotatsu::cli::Endpoint& endpoint) {
        kotatsu::cli::EndpointFields fields;
        if(!codec::decode_value<F>(reader, fields)) {
            return false;
        }
        if(fields.host.empty()) {
            return codec::scoped_context<codec::rich_error>::fail(codec::rich_error("no host"));
        }
        endpoint.host = fields.host;
        return true;
    }
};

namespace kotatsu::cli {

namespace {

ZEST_SUITE(cli_usage) {

ZEST_CASE(usage_lists_options_with_their_help) {
    ZEXPECT(cli::usage<Tool>("tool", {"serve"}) ==
            "Start the server\n"
            "\n"
            "usage: tool serve [<PATH>...] [OPTIONS]\n"
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

ZEST_CASE(usage_lists_subcommands_in_declaration_order) {
    ZEXPECT(cli::usage<Tool>("tool") ==
            "A tool\n"
            "\n"
            "usage: tool [OPTIONS] <command>\n"
            "\n"
            "Options:\n"
            "  -h|--help                       display this help and exit\n"
            "\n"
            "Subcommands:\n"
            "  serve  Start the server\n"
            "  query  Ask the index\n");
}

ZEST_CASE(long_usage_puts_its_help_below) {
    const auto usage = cli::usage<Serve>("serve", {}, Style{.help_column = 10});
    ZEXPECT(usage.contains("  --mode <pipe|socket>\n            Server mode [default: pipe]\n"));
}

ZEST_CASE(color_look_paints_headings) {
    const auto usage = cli::usage<Tool>("tool", {}, Style{.color = true});
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
    ZASSERT(entries.size() == 2U);
    ZEXPECT(entries[1].kind == UsageEntry::Kind::Positional);
    ZEXPECT(entries[1].usage == "-- <ARG>...");
    ZEXPECT(!entries[1].repeatable);
}

ZEST_CASE(hidden_ones_are_left_out) {
    const auto entries = cli::usage_entries<Internal>();
    ZASSERT(entries.size() == 2U);
    ZEXPECT(entries[0].usage == "-h|--help");
    ZEXPECT(entries[1].usage == "query");
}

ZEST_CASE(hidden_ones_still_parse) {
    auto internal = cli::parse<Internal>(std::vector<std::string>{"--trace", "serve"});
    ZASSERT(internal.has_value());
    ZEXPECT(internal->trace);
    ZEXPECT(internal->command.index() == 0U);
}

ZEST_CASE(entries_say_defaults_and_ranges) {
    const auto entries = cli::usage_entries<Tuned>();
    ZASSERT(entries.size() == 5U);
    ZEXPECT(entries[1].default_value == "4");
    ZEXPECT(entries[1].range == "1..64");
    ZEXPECT(entries[3].default_value == "a, b");
    ZEXPECT(entries[4].default_value.empty());
}

ZEST_CASE(choices_are_the_value_token) {
    const auto entries = cli::usage_entries<Tuned>();
    ZASSERT(entries.size() == 5U);
    ZEXPECT(entries[2].usage == "--mode <fast|slow>");
    ZEXPECT(entries[2].default_value == "fast");
}

ZEST_CASE(help_says_default_and_range) {
    ZEXPECT(cli::usage<Tuned>("tuned").contains("Workers [default: 4] [range: 1..64]"));
}

ZEST_CASE(program_s_defaults_are_written_without_being_read) {
    ZEXPECT(cli::usage<Endpoint>("tool").contains("[default: localhost]"));
}

ZEST_CASE(subcommand_a_repr_refuses_to_read_says_no_defaults) {
    const auto usage = cli::usage<Connect>("tool", {"endpoint"});
    ZEXPECT(usage.contains("--host <value>"));
    ZEXPECT(!usage.contains("[default:"));
}

ZEST_CASE(help_entry_lists_the_names_the_command_leaves_it) {
    const auto names_h = cli::usage_entries<NamesH>();
    ZASSERT(names_h.size() == 2U);
    ZEXPECT(names_h[0].usage == "--help");
    ZEXPECT(names_h[1].usage == "-h|--host <value>");
    const auto slash_h = cli::usage_entries<SlashH>();
    ZASSERT(slash_h.size() == 2U);
    ZEXPECT(slash_h[0].usage == "--help");
    const auto help_field = cli::usage_entries<HelpField>();
    ZASSERT(help_field.size() == 2U);
    ZEXPECT(help_field[0].usage == "-h");
    ZEXPECT(help_field[1].usage == "--help");
}

ZEST_CASE(command_taking_both_names_lists_no_help_entry) {
    const auto entries = cli::usage_entries<BothNames>();
    ZASSERT(entries.size() == 1U);
    ZEXPECT(entries[0].usage == "-h|--help");
    ZEXPECT(entries[0].help.empty());
}

ZEST_CASE(required_option_has_no_default) {
    const auto entries = cli::usage_entries<Must>();
    ZASSERT(entries.size() == 2U);
    ZEXPECT(entries[1].default_value.empty());
}

ZEST_CASE(value_that_does_not_write_has_no_default) {
    const auto entries = cli::usage_entries<Unnamed>();
    ZASSERT(entries.size() == 3U);
    ZEXPECT(entries[1].default_value.empty());
    ZEXPECT(entries[2].default_value == "2");
}

};  // ZEST_SUITE(cli_usage)

}  // namespace

}  // namespace kotatsu::cli
