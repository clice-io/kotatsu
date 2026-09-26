#include <string>
#include <vector>

#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco {
namespace {

enum class DescEnum {
    Alpha,
    Beta,
    Gamma,
};

struct DescOpt {
    DecoFlag(names = {"-v", "--verbose"}; help = "Show version and exit"; required = false;)
    verbose;

    DecoKV(names = {"-o", "--output"}; meta_var = "FILE"; help = "Write output to FILE";
           required = false;)
    <std::string> output;

    DecoKVStyled(decl::KVStyle::Joined, names = {"-I", "--include"}; meta_var = "DIR";
                 help = "Add include search path";
                 required = false;)
    <std::string> include_dir;

    DecoKVStyled(decl::KVStyle::JoinedOrSeparate, names = {"--filter"}; meta_var = "PATTERN";
                 help = "Filter tests";
                 required = false;)
    <std::string> filter;

    DecoComma(names = {"--tags", "-T"}; meta_var = "TAG"; help = "Comma-separated tags";
              required = false;)
    <std::vector<std::string>> tags;

    DecoMulti(2, names = {"--pair"}; meta_var = "VAL"; help = "Two values"; required = false;)
    <std::vector<std::string>> pair;

    DecoInput(meta_var = "INPUT"; help = "Input file"; required = false;)
    <std::string> input;
    DecoPack(meta_var = "ARG"; help = "Trailing arguments"; required = false;)
    <std::vector<std::string>> trailing;

    DecoFlag(help = "Unnamed flag fallback"; required = false;)
    unnamed;
};

struct VectorInputDescOpt {
    DecoInput(meta_var = "INPUT"; help = "Input files"; required = false;)
    <std::vector<std::string>> inputs;
};

struct NoHelpDescOpt {
    DecoFlag(required = false;)
    no_help_flag;
};

struct EnumVectorDescOpt {
    DecoComma(required = false;)
    <std::vector<DescEnum>> values;

    DecoMulti(2, required = false;)
    <std::vector<DescEnum>> pair;

    DecoInput(required = false;)
    <std::vector<DescEnum>> inputs;
};

ZEST_SUITE(deco_descriptor) {

ZEST_CASE(from_deco_option_renders_usage_style_text) {
    DescOpt opt{};

    EXPECT(desc::from_deco_option(opt.verbose) == "-v|--verbose");
    EXPECT(desc::from_deco_option(opt.output) == "-o|--output <FILE>");
    EXPECT(desc::from_deco_option(opt.include_dir) == "-I<DIR>|--include=<DIR>");
    EXPECT(desc::from_deco_option(opt.filter) == "--filter <PATTERN>|--filter=<PATTERN>");
    EXPECT(desc::from_deco_option(opt.tags) == "--tags,<TAG>[,<TAG>...]|-T,<TAG>[,<TAG>...]");
    EXPECT(desc::from_deco_option(opt.pair) == "--pair <VAL1> <VAL2>");
    EXPECT(desc::from_deco_option(opt.input) == "<INPUT>");
    EXPECT(desc::from_deco_option(opt.trailing) == "-- <ARG>...");
    EXPECT(desc::from_deco_option(opt.unnamed) == "--<flag>");
    EXPECT(desc::from_deco_option(opt.unnamed, false, "u") == "-u");
    EXPECT(desc::from_deco_option(opt.unnamed, false, "long_name") == "--long-name");
}

ZEST_CASE(from_deco_option_renders_help_style_text) {
    DescOpt opt{};

    const auto verbose_help = desc::from_deco_option(opt.verbose, true);
    EXPECT(zest::contains(verbose_help, "-v, --verbose"));
    EXPECT(zest::contains(verbose_help, "Show version and exit"));

    const auto output_help = desc::from_deco_option(opt.output, true);
    EXPECT(zest::contains(output_help, "-o, --output <FILE>"));
    EXPECT(zest::contains(output_help, "Write output to FILE"));

    const auto filter_help = desc::from_deco_option(opt.filter, true);
    EXPECT(zest::contains(filter_help, "--filter <PATTERN>, --filter=<PATTERN>"));
    EXPECT(zest::contains(filter_help, "Filter tests"));

    const auto input_help = desc::from_deco_option(opt.input, true);
    EXPECT(zest::contains(input_help, "<INPUT>"));
    EXPECT(zest::contains(input_help, "Input file"));

    VectorInputDescOpt vector_opt{};
    EXPECT(desc::from_deco_option(vector_opt.inputs) == "<INPUT>...");
    const auto vector_input_help = desc::from_deco_option(vector_opt.inputs, true);
    EXPECT(zest::contains(vector_input_help, "<INPUT>..."));
    EXPECT(zest::contains(vector_input_help, "Input files"));
}

ZEST_CASE(from_deco_option_uses_configured_help_layout_and_default_help) {
    DescOpt opt{};
    NoHelpDescOpt no_help_opt{};
    auto config = config::get();
    config.render.compatible.usage.help_column = 10;
    config.render.compatible.usage.default_help = "configured help text";

    const auto verbose_help = desc::from_deco_option(opt.verbose, true, {}, &config);
    EXPECT(zest::contains(verbose_help, "-v, --verbose"));
    EXPECT(zest::contains(verbose_help, "\n"));
    EXPECT(zest::contains(verbose_help, "Show version and exit"));

    const auto no_help = desc::from_deco_option(no_help_opt.no_help_flag, true, {}, &config);
    EXPECT(zest::contains(no_help, "configured help text"));
}

ZEST_CASE(from_deco_option_uses_override_config_for_non_option_help) {
    NoHelpDescOpt no_help_opt{};
    auto config = config::get();
    config.render.compatible.usage.default_help = "fallback from override";

    const auto no_help = desc::from_deco_option(no_help_opt.no_help_flag, true, {}, &config);
    EXPECT(zest::contains(no_help, "fallback from override"));
}

ZEST_CASE(from_deco_option_infers_enum_meta_var_for_vector_results) {
    EnumVectorDescOpt opt{};

    EXPECT(desc::from_deco_option(opt.values, false, "values") ==
           "--values,<alpha|beta|gamma>[,<alpha|beta|gamma>...]");
    EXPECT(desc::from_deco_option(opt.pair, false, "pair") ==
           "--pair <alpha|beta|gamma> <alpha|beta|gamma>");
    EXPECT(desc::from_deco_option(opt.inputs) == "<alpha|beta|gamma>...");
}

};  // ZEST_SUITE(deco_descriptor)

}  // namespace
}  // namespace kota::deco
