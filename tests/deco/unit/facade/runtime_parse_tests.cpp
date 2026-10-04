#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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

struct Output {
    DecoKV(required = false;)
    <std::string> out_path;
};

struct Kinds {
    DECO_CFG_START(required = false);

    DecoFlag(names = {"-v", "--verbose"};)
    verbose;

    DecoFlagN(names = {"-d"};)
    debug;

    DecoKV()
    <int> level;

    DecoKVStyled(decl::KVStyle::Joined, names = {"-I"};)
    <std::string> include;

    DecoComma(names = {"--tags"};)
    <std::vector<std::string>> tags;

    DecoMulti(2, names = {"--pair"};)
    <std::vector<int>> pair;

    Output output;

    DecoInput()
    <std::string> input;

    DecoPack()
    <std::vector<std::string>> pack;

    DECO_CFG_END();
};

struct Inputs {
    DecoInput(required = false;)
    <std::vector<std::string>> files;
};

struct NoInput {
    DecoFlag(required = false;)
    verbose;
};

struct PackOnly {
    DecoPack(required = false;)
    <std::vector<std::string>> rest;
};

/// Fails without a message, and says where itself.
struct Silent {
    std::optional<std::string> into(std::string_view, const decl::IntoContext&) {
        return std::string();
    }
};

struct SilentFailure {
    DecoKV(required = false;)
    <Silent> value;

    DecoFlag(required = false;)
    after;
};

struct Required {
    DecoKV()
    <std::string> must;

    DecoFlag()
    other = false;
};

constexpr decl::Category mode_category{
    .exclusive = false,
    .required = true,
    .name = "mode",
    .description = "what to do",
};

struct RequiredCategory {
    DecoFlag(names = {"--list"}; required = false; category = mode_category;)
    list;

    DecoFlag(names = {"-q"}; required = false;)
    quiet;
};

struct RequiredPack {
    DecoPack(required = false; category = mode_category;)
    <std::vector<std::string>> command;

    DecoFlag(names = {"-q"}; required = false;)
    quiet;
};

struct Script {
    DecoFlag(required = false;)
    v;

    DecoInput(required = false;)
    <std::string> script;

    DecoKV(required = false;)
    <std::string> s;
};

struct ScriptArgs {
    DecoInput(required = false;)
    <std::vector<std::string>> args;

    DecoPack(required = false;)
    <std::vector<std::string>> command;
};

ZEST_SUITE(deco_facade_runtime_parse) {

ZEST_CASE(options_of_each_kind_are_set) {
    auto argv = test::split(
        "-v -d -d --level 3 -I/usr/include --tags,a,b --pair 1 2 --out-path out main.cc -- x -y");
    const auto parsed = cli::parse<Kinds>(argv);
    ZASSERT(parsed.has_value());
    const auto& options = parsed->options;
    ZEXPECT(options.verbose.as_optional() == std::optional(true));
    ZEXPECT(options.debug.as_optional() == std::optional(2U));
    ZEXPECT(options.level.as_optional() == std::optional(3));
    ZEXPECT(options.include.as_optional() == std::optional<std::string>("/usr/include"));
    ZEXPECT(options.tags.as_optional() == std::optional(strings{"a", "b"}));
    ZEXPECT(options.pair.as_optional() == std::optional(std::vector<int>{1, 2}));
    ZEXPECT(options.output.out_path.as_optional() == std::optional<std::string>("out"));
    ZEXPECT(options.input.as_optional() == std::optional<std::string>("main.cc"));
    ZEXPECT(options.pack.as_optional() == std::optional(strings{"x", "-y"}));
}

ZEST_CASE(nothing_given_sets_nothing) {
    std::vector<std::string> argv;
    const auto parsed = cli::parse<Kinds>(argv);
    ZASSERT(parsed.has_value());
    ZEXPECT(!parsed->options.verbose.has_value());
    ZEXPECT(!parsed->options.pack.has_value());
    ZEXPECT(parsed->matched_categories.empty());
}

ZEST_CASE(repeated_option_keeps_the_last_value) {
    auto argv = test::split("--level 1 --level 2");
    const auto parsed = cli::parse<Kinds>(argv);
    ZASSERT(parsed.has_value());
    ZEXPECT(parsed->options.level.as_optional() == std::optional(2));
}

ZEST_CASE(input_list_gathers_every_positional) {
    auto argv = test::split("a b c");
    const auto parsed = cli::parse<Inputs>(argv);
    ZASSERT(parsed.has_value());
    ZEXPECT(parsed->options.files.as_optional() == std::optional(strings{"a", "b", "c"}));
}

ZEST_CASE(input_and_pack_match_their_categories) {
    auto argv = test::split("main.cc -- a");
    const auto parsed = cli::parse<Kinds>(argv);
    ZASSERT(parsed.has_value());
    ZEXPECT(parsed->matched(decl::default_category));
    ZEXPECT(parsed->matched_categories.size() == 1U);
}

ZEST_CASE(unknown_option_fails) {
    auto argv = test::split("-v --nope");
    const auto parsed = cli::parse<Kinds>(argv);
    ZASSERT(!parsed.has_value());
    ZEXPECT(parsed.error().type == cli::ParseError::Type::BackendParsing);
    ZEXPECT(parsed.error().message ==
            "at argv[1]:\n  -v --nope\n     ^~~~~~\n  unknown option '--nope'");
}

ZEST_CASE(missing_value_fails) {
    auto argv = test::split("-v --level");
    const auto parsed = cli::parse<Kinds>(argv);
    ZASSERT(!parsed.has_value());
    ZEXPECT(parsed.error().type == cli::ParseError::Type::BackendParsing);
    ZEXPECT(zest::starts_with(parsed.error().message, "at argv[1]:"));
    ZEXPECT(zest::ends_with(parsed.error().message, "missing argument value"));
}

ZEST_CASE(unparsable_value_fails) {
    auto argv = test::split("--level x");
    const auto parsed = cli::parse<Kinds>(argv);
    ZASSERT(!parsed.has_value());
    ZEXPECT(parsed.error().type == cli::ParseError::Type::IntoError);
    ZEXPECT(zest::starts_with(parsed.error().message, "at argv[1]:"));
    ZEXPECT(zest::ends_with(parsed.error().message, "invalid integer value: x"));
}

ZEST_CASE(custom_value_fails) {
    auto argv = test::split("-X INVALID");
    const auto parsed = cli::parse<WebCli>(argv);
    ZASSERT(!parsed.has_value());
    ZEXPECT(parsed.error().type == cli::ParseError::Type::IntoError);
    ZEXPECT(zest::starts_with(parsed.error().message, "at argv[1]:"));
    ZEXPECT(
        zest::ends_with(parsed.error().message, "Invalid request type. Expected 'GET' or 'POST'."));
}

ZEST_CASE(error_without_a_message_still_fails) {
    auto argv = test::split("--value x --after");
    const auto parsed = cli::parse<SilentFailure>(argv);
    ZASSERT(!parsed.has_value());
    ZEXPECT(parsed.error().type == cli::ParseError::Type::IntoError);
    ZEXPECT(parsed.error().message.empty());
}

ZEST_CASE(positional_without_an_input_fails) {
    auto argv = test::split("main.cc");
    const auto parsed = cli::parse<NoInput>(argv);
    ZASSERT(!parsed.has_value());
    ZEXPECT(parsed.error().type == cli::ParseError::Type::DecoParsing);
    ZEXPECT(zest::ends_with(parsed.error().message, "unexpected input argument main.cc"));
}

ZEST_CASE(positional_before_dash_dash_without_an_input_fails) {
    auto argv = test::split("front -- a");
    const auto parsed = cli::parse<PackOnly>(argv);
    ZASSERT(!parsed.has_value());
    ZEXPECT(parsed.error().type == cli::ParseError::Type::DecoParsing);
    ZEXPECT(zest::ends_with(parsed.error().message, "unexpected input argument front"));
}

ZEST_CASE(pack_takes_everything_after_dash_dash) {
    auto argv = test::split("-- -v --nope");
    const auto parsed = cli::parse<PackOnly>(argv);
    ZASSERT(parsed.has_value());
    ZEXPECT(parsed->options.rest.as_optional() == std::optional(strings{"-v", "--nope"}));
}

ZEST_CASE(dash_dash_without_a_pack_fails) {
    auto argv = test::split("-- a");
    const auto parsed = cli::parse<NoInput>(argv);
    ZASSERT(!parsed.has_value());
    ZEXPECT(parsed.error().type == cli::ParseError::Type::BackendParsing);
    ZEXPECT(zest::ends_with(parsed.error().message, "unknown option '--'"));
}

ZEST_CASE(dash_dash_to_positionals_alone_fails) {
    // With no option to take a prefix from, "--" reads as a positional, the one a pack takes.
    auto argv = test::split("a -- b");
    const auto parsed = cli::parse<Inputs>(argv);
    ZASSERT(!parsed.has_value());
    ZEXPECT(parsed.error().type == cli::ParseError::Type::DecoParsing);
    ZEXPECT(zest::ends_with(parsed.error().message, "unexpected trailing argument --"));
}

ZEST_CASE(required_option_missing_fails) {
    auto argv = test::split("--other");
    const auto parsed = cli::parse<Required>(argv);
    ZASSERT(!parsed.has_value());
    ZEXPECT(parsed.error().type == cli::ParseError::Type::DecoParsing);
    ZEXPECT(parsed.error().message ==
            "at end of argv:\n  --other\n         ^\n  required option --must <value> is missing");
}

ZEST_CASE(required_option_of_an_unmatched_category_is_not_missing) {
    auto argv = test::split("--version");
    const auto parsed = cli::parse<WebCli>(argv);
    ZASSERT(parsed.has_value());
    ZEXPECT(parsed->matched(WebCli::version_category));
    ZEXPECT(!parsed->matched(WebCli::request_category));
}

ZEST_CASE(required_option_of_a_matched_category_fails) {
    auto argv = test::split("-X GET");
    const auto parsed = cli::parse<WebCli>(argv);
    ZASSERT(!parsed.has_value());
    ZEXPECT(parsed.error().type == cli::ParseError::Type::DecoParsing);
    ZEXPECT(zest::ends_with(parsed.error().message, "required option --url <URL> is missing"));
}

ZEST_CASE(required_category_missing_fails) {
    auto argv = test::split("-q");
    const auto parsed = cli::parse<RequiredCategory>(argv);
    ZASSERT(!parsed.has_value());
    ZEXPECT(parsed.error().type == cli::ParseError::Type::DecoParsing);
    ZEXPECT(zest::ends_with(parsed.error().message, "required <mode> (what to do) is missing"));
}

ZEST_CASE(required_category_of_a_pack_missing_fails) {
    auto missing = test::split("-q");
    const auto without = cli::parse<RequiredPack>(missing);
    ZASSERT(!without.has_value());
    ZEXPECT(zest::ends_with(without.error().message, "required <mode> (what to do) is missing"));

    auto given = test::split("-q -- make");
    ZEXPECT(cli::parse<RequiredPack>(given).has_value());
}

ZEST_CASE(exclusive_category_with_another_fails) {
    auto argv = test::split("--version --help");
    const auto parsed = cli::parse<WebCli>(argv);
    ZASSERT(!parsed.has_value());
    ZEXPECT(parsed.error().type == cli::ParseError::Type::DecoParsing);
    ZEXPECT(zest::contains(parsed.error().message,
                           "are exclusive, but multiple categories are matched"));
}

ZEST_CASE(error_through_the_renderer_given_fails) {
    auto argv = test::split("--nope");
    const auto renderer = test::tagged_renderer();
    const auto parsed = cli::parse<Kinds>(argv, renderer);
    ZASSERT(!parsed.has_value());
    ZEXPECT(parsed.error().message == "ERR<0:unknown option '--nope'>");
}

ZEST_CASE(error_with_positions_off_fails) {
    test::ScopedDecoConfig restore;
    auto config = config::get();
    config.render.compatible.diagnostic.enabled = false;
    config::set(config);

    auto argv = test::split("--nope");
    const auto parsed = cli::parse<Kinds>(argv);
    ZASSERT(!parsed.has_value());
    ZEXPECT(parsed.error().message == "unknown option '--nope'");
}

ZEST_CASE(invocation_reports_how_far_it_got) {
    auto argv = test::split("-v --level 3");
    const auto parsed = cli::invoke<Kinds>(argv);
    ZASSERT(parsed.has_value());
    ZEXPECT(parsed->next_cursor() == 3U);
    ZEXPECT(parsed->argv().data() == argv.data());
    ZEXPECT(parsed->remaining().empty());
    ZASSERT(parsed->trace().size() == 2U);
    ZEXPECT(parsed->trace()[0].spelling == "-v");
    ZEXPECT(parsed->trace()[1].values == strings{"3"});
}

ZEST_CASE(parse_with_callback_stops_where_told) {
    auto argv = test::split("-v script::cdb -t x -- make");
    const auto parsed =
        cli::parse_with_callback<Script>(argv, [](const Script& options, decl::DecoOptionBase* at) {
            return at != &options.script;
        });
    ZASSERT(parsed.has_value());
    ZEXPECT(parsed->options.v.as_optional() == std::optional(true));
    ZEXPECT(parsed->options.script.as_optional() == std::optional<std::string>("script::cdb"));
    ZASSERT(parsed->next_cursor() == 2U);

    // What is left parses on its own.
    const auto rest = cli::parse<ScriptArgs>(parsed->remaining());
    ZASSERT(rest.has_value());
    ZEXPECT(rest->options.args.as_optional() == std::optional(strings{"-t", "x"}));
    ZEXPECT(rest->options.command.as_optional() == std::optional(strings{"make"}));
}

ZEST_CASE(argvify_skips_the_program_name) {
    const char* const argv[] = {"app", "-v", "x"};
    ZEXPECT(util::argvify(3, argv) == (strings{"-v", "x"}));
    ZEXPECT(util::argvify(3, argv, 0) == (strings{"app", "-v", "x"}));
    ZEXPECT(util::argvify(0, argv).empty());
}

};  // ZEST_SUITE(deco_facade_runtime_parse)

}  // namespace

}  // namespace kota::deco
