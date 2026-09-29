#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "deco/harness/argv.h"
#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco {

namespace {

enum class Level {
    Low,
    Mid,
    High,
};

enum class Many {
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
};

struct Described {
    DecoFlag(names = {"-v", "--verbose"}; help = "Show more"; required = false;)
    verbose;

    DecoKV(names = {"-o", "--output"}; meta_var = "FILE"; help = "Write output to FILE";
           required = false;)
    <std::string> output;

    DecoComma(names = {"--tags", "-T"}; meta_var = "TAG"; required = false;)
    <std::vector<std::string>> tags;

    DecoMulti(2, names = {"--pair"}; meta_var = "VAL"; required = false;)
    <std::vector<std::string>> pair;

    DecoInput(meta_var = "INPUT"; required = false;)
    <std::string> input;

    DecoPack(meta_var = "ARG"; required = false;)
    <std::vector<std::string>> trailing;

    DecoFlag(required = false;)
    unnamed;

    DecoFlagAlias(names = {"-O1"}; forward = {"--level", "1"}; help = "Optimize a little";) _;
};

/// A KV option in each way one can be named, with a value of "x" in each form.
struct Forms {
    DECO_CFG_START(required = false);

    DecoKV(names = {"-o", "--output"};)
    <std::string> output;

    DecoKVStyled(decl::KVStyle::Joined, names = {"-I", "--include"};)
    <std::string> include;

    DecoKVStyled(decl::KVStyle::JoinedOrSeparate, names = {"--flags", "--flags="};)
    <std::string> flags;

    DecoKVStyled(decl::KVStyle::JoinedOrSeparate, names = {"--filter"};)
    <std::string> filter;

    DecoKVStyled(decl::KVStyle::JoinedOrSeparate, names = {"/Fo:"};)
    <std::string> object;

    DecoKVStyled(decl::KVStyle::JoinedOrSeparate)
    <std::string> level_name;

    DecoKVStyled(decl::KVStyle::JoinedOrSeparate)
    <std::string> x;

    DecoKVStyled(decl::KVStyle::Joined)
    <std::string> joined;

    DECO_CFG_END();
};

struct Enums {
    DecoKV(required = false;)
    <Level> level;

    DecoKV(meta_var = "L"; required = false;)
    <Level> named;

    DecoComma(required = false;)
    <std::vector<Level>> levels;

    DecoMulti(2, required = false;)
    <std::vector<Level>> pair;

    DecoInput(required = false;)
    <std::vector<Level>> inputs;

    DecoKV(required = false;)
    <Many> many;
};

struct VectorInput {
    DecoInput(meta_var = "FILE"; required = false;)
    <std::vector<std::string>> files;
};

/// The argv each form `usage` shows is written with, the value "x": "-o|--output <FILE>"
/// shows "-o x" and "--output x", "--output=<FILE>" shows "--output=x".
std::vector<std::vector<std::string>> written_forms(std::string_view usage) {
    std::vector<std::vector<std::string>> forms;
    // Names that take their value separate share it, after the last of them.
    std::vector<std::string> separate;
    for(const auto& form: test::split(usage, '|')) {
        if(const auto space = form.find(' '); space != std::string::npos) {
            separate.push_back(form.substr(0, space));
            for(const auto& name: separate) {
                forms.push_back({name, "x"});
            }
            separate.clear();
        } else if(const auto value = form.find('<'); value != std::string::npos) {
            forms.push_back({form.substr(0, value) + "x"});
        } else {
            separate.push_back(form);
        }
    }
    return forms;
}

ZEST_SUITE(deco_facade_descriptor) {

ZEST_CASE(flag_shows_its_names) {
    const Described options{};
    EXPECT(desc::from_deco_option(options.verbose) == "-v|--verbose");
}

ZEST_CASE(separate_kv_shows_its_names_then_the_value) {
    const Described options{};
    EXPECT(desc::from_deco_option(options.output) == "-o|--output <FILE>");
}

ZEST_CASE(kv_shows_each_name_as_the_parser_takes_it) {
    const Forms options{};
    EXPECT(desc::from_deco_option(options.include) == "-I<value>|--include<value>");
    EXPECT(desc::from_deco_option(options.flags) == "--flags <value>|--flags=<value>");
    EXPECT(desc::from_deco_option(options.filter) == "--filter <value>");
    EXPECT(desc::from_deco_option(options.object) == "/Fo:<value>");
}

ZEST_CASE(kv_generated_name_also_takes_its_value_after_equals) {
    const Forms options{};
    EXPECT(desc::from_deco_option(options.level_name, false, "level_name") ==
           "--level-name <value>|--level-name=<value>");
    EXPECT(desc::from_deco_option(options.x, false, "x") == "-x <value>|-x=<value>");
    EXPECT(desc::from_deco_option(options.joined, false, "joined") == "--joined=<value>");
}

ZEST_CASE(every_kv_form_shown_parses) {
    std::vector<std::pair<std::string, std::vector<std::string>>> shown;
    detail::generator_of<Forms>().visit_fields(
        Forms{},
        [&](const auto& field, const auto&, std::string_view name, auto) {
            for(auto& argv: written_forms(desc::from_deco_option(field, false, name))) {
                shown.emplace_back(name, std::move(argv));
            }
            return true;
        });
    EXPECT(shown.size() == 13U);

    for(auto& [name, argv]: shown) {
        ZEST_CONTEXT("{} written {}", name, argv.front());
        auto parsed = cli::parse<Forms>(argv);
        ASSERT(parsed.has_value());
        std::optional<std::string> value;
        detail::generator_of<Forms>().visit_fields(
            parsed->options,
            [&](const auto& field, const auto&, std::string_view field_name, auto) {
                if(field_name == name) {
                    value = field.as_optional();
                }
                return true;
            });
        EXPECT(value == std::optional<std::string>("x"));
    }
}

ZEST_CASE(help_mode_separates_names_with_commas) {
    const Described options{};
    EXPECT(
        zest::starts_with(desc::from_deco_option(options.output, true), "  -o, --output <FILE>"));
}

ZEST_CASE(comma_shows_the_list_after_each_name) {
    const Described options{};
    EXPECT(desc::from_deco_option(options.tags) == "--tags,<TAG>[,<TAG>...]|-T,<TAG>[,<TAG>...]");
}

ZEST_CASE(multi_numbers_its_values) {
    const Described options{};
    EXPECT(desc::from_deco_option(options.pair) == "--pair <VAL1> <VAL2>");
}

ZEST_CASE(input_and_pack_show_their_values) {
    const Described options{};
    EXPECT(desc::from_deco_option(options.input) == "<INPUT>");
    EXPECT(desc::from_deco_option(options.trailing) == "-- <ARG>...");
    const VectorInput vector{};
    EXPECT(desc::from_deco_option(vector.files) == "<FILE>...");
}

ZEST_CASE(unnamed_option_takes_its_field_name) {
    const Described options{};
    EXPECT(desc::from_deco_option(options.unnamed, false, "u") == "-u");
    EXPECT(desc::from_deco_option(options.unnamed, false, "long_name") == "--long-name");
}

ZEST_CASE(unnamed_option_without_a_name_shows_a_placeholder) {
    const Described options{};
    EXPECT(desc::from_deco_option(options.unnamed) == "--<flag>");
    EXPECT(desc::from_deco_option(options.unnamed, false, "_") == "--<flag>");
}

ZEST_CASE(alias_shows_its_names) {
    const Described options{};
    EXPECT(desc::from_deco_option(options._, false, "_") == "-O1");
}

ZEST_CASE(meta_var_is_bracketed_once) {
    EXPECT(desc::detail::meta_var_token("FILE") == "<FILE>");
    EXPECT(desc::detail::meta_var_token("<FILE>") == "<FILE>");
    EXPECT(desc::detail::meta_var_token("") == "<value>");
}

ZEST_CASE(enum_value_shows_its_names) {
    const Enums options{};
    EXPECT(desc::from_deco_option(options.level, false, "level") == "--level <low|mid|high>");
    EXPECT(desc::from_deco_option(options.levels, false, "levels") ==
           "--levels,<low|mid|high>[,<low|mid|high>...]");
    EXPECT(desc::from_deco_option(options.pair, false, "pair") ==
           "--pair <low|mid|high> <low|mid|high>");
    EXPECT(desc::from_deco_option(options.inputs) == "<low|mid|high>...");
}

ZEST_CASE(enum_names_past_the_limit_are_cut) {
    const Enums options{};
    EXPECT(desc::from_deco_option(options.many, false, "many") == "--many <a|b|c|d|e|f|...>");
}

ZEST_CASE(explicit_meta_var_beats_enum_names) {
    const Enums options{};
    EXPECT(desc::from_deco_option(options.named, false, "named") == "--named <L>");
}

ZEST_CASE(enum_names_can_be_turned_off) {
    const Enums options{};
    auto config = config::get();
    config.enum_meta_var.enabled = false;
    EXPECT(desc::from_deco_option(options.level, false, "level", &config) == "--level <value>");
}

ZEST_CASE(enum_names_follow_the_config) {
    const Enums options{};
    auto config = config::get();
    config.enum_meta_var.max_items = 2;
    config.enum_meta_var.separator = ",";
    config.enum_meta_var.overflow_suffix = ",etc";
    EXPECT(desc::from_deco_option(options.level, false, "level", &config) ==
           "--level <low,mid,etc>");
}

ZEST_CASE(help_line_pads_the_usage_to_the_help_column) {
    const Described options{};
    auto config = config::get();
    config.render.compatible.usage.help_column = 16;
    EXPECT(desc::from_deco_option(options.verbose, true, {}, &config) ==
           "  -v, --verbose   Show more");
}

ZEST_CASE(help_line_of_a_long_usage_starts_below_at_the_help_column) {
    const Described options{};
    auto config = config::get();
    config.render.compatible.usage.help_column = 8;
    EXPECT(desc::from_deco_option(options.output, true, {}, &config) ==
           "  -o, --output <FILE>\n          Write output to FILE");
}

ZEST_CASE(help_line_without_help_shows_the_default) {
    const Described options{};
    auto config = config::get();
    config.render.compatible.usage.help_column = 16;
    config.render.compatible.usage.default_help = "nothing to say";
    EXPECT(desc::from_deco_option(options.unnamed, true, "unnamed", &config) ==
           "  --unnamed       nothing to say");
}

ZEST_CASE(category_shows_what_it_has) {
    EXPECT(desc::detail::category_desc({.name = "mode", .description = "one mode"}) ==
           "<mode> (one mode)");
    EXPECT(desc::detail::category_desc({.name = "mode", .description = ""}) == "<mode>");
    EXPECT(desc::detail::category_desc({.name = "", .description = "one mode"}) == "one mode");
    EXPECT(desc::detail::category_desc({}) == "<unnamed category>");
}

};  // ZEST_SUITE(deco_facade_descriptor)

}  // namespace

}  // namespace kota::deco
