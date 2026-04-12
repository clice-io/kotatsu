#include <string>
#include <string_view>
#include <vector>

#include "eventide/deco/deco.h"
#include <eventide/zest/zest.h>

enum class DescEnum {
    Alpha,
    Beta,
    Gamma,
};

namespace {

struct DescOpt {
    DecoFlag(names = {"-v", "--verbose"}; help = "Show version and exit"; required = false;)
    verbose;

    DecoKV(names = {"-o", "--output"}; meta_var = "FILE"; help = "Write output to FILE";
           required = false;)
    <std::string> output;

    DecoKVStyled(deco::decl::KVStyle::Joined, names = {"-I", "--include"}; meta_var = "DIR";
                 help = "Add include search path";
                 required = false;)
    <std::string> include_dir;

    DecoKVStyled(static_cast<char>(deco::decl::KVStyle::Joined | deco::decl::KVStyle::Separate),
                 names = {"--filter"};
                 meta_var = "PATTERN";
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

}  // namespace

TEST_SUITE(deco_descriptor) {

TEST_CASE(from_deco_option_renders_usage_style_text) {
    DescOpt opt{};

    EXPECT_TRUE(deco::desc::from_deco_option(opt.verbose) == "-v|--verbose");
    EXPECT_TRUE(deco::desc::from_deco_option(opt.output) == "-o|--output <FILE>");
    EXPECT_TRUE(deco::desc::from_deco_option(opt.include_dir) == "-I<DIR>|--include=<DIR>");
    EXPECT_TRUE(deco::desc::from_deco_option(opt.filter) ==
                "--filter <PATTERN>|--filter=<PATTERN>");
    EXPECT_TRUE(deco::desc::from_deco_option(opt.tags) ==
                "--tags,<TAG>[,<TAG>...]|-T,<TAG>[,<TAG>...]");
    EXPECT_TRUE(deco::desc::from_deco_option(opt.pair) == "--pair <VAL1> <VAL2>");
    EXPECT_TRUE(deco::desc::from_deco_option(opt.input) == "<INPUT>");
    EXPECT_TRUE(deco::desc::from_deco_option(opt.trailing) == "-- <ARG>...");
    EXPECT_TRUE(deco::desc::from_deco_option(opt.unnamed) == "--<flag>");
    EXPECT_TRUE(deco::desc::from_deco_option(opt.unnamed, false, "u") == "-u");
    EXPECT_TRUE(deco::desc::from_deco_option(opt.unnamed, false, "long_name") == "--long-name");
}

TEST_CASE(from_deco_option_renders_help_style_text) {
    DescOpt opt{};

    const auto verbose_help = deco::desc::from_deco_option(opt.verbose, true);
    EXPECT_TRUE(verbose_help.find("-v, --verbose") != std::string::npos);
    EXPECT_TRUE(verbose_help.find("Show version and exit") != std::string::npos);

    const auto output_help = deco::desc::from_deco_option(opt.output, true);
    EXPECT_TRUE(output_help.find("-o, --output <FILE>") != std::string::npos);
    EXPECT_TRUE(output_help.find("Write output to FILE") != std::string::npos);

    const auto filter_help = deco::desc::from_deco_option(opt.filter, true);
    EXPECT_TRUE(filter_help.find("--filter <PATTERN>, --filter=<PATTERN>") != std::string::npos);
    EXPECT_TRUE(filter_help.find("Filter tests") != std::string::npos);

    const auto input_help = deco::desc::from_deco_option(opt.input, true);
    EXPECT_TRUE(input_help.find("<INPUT>") != std::string::npos);
    EXPECT_TRUE(input_help.find("Input file") != std::string::npos);

    VectorInputDescOpt vector_opt{};
    EXPECT_TRUE(deco::desc::from_deco_option(vector_opt.inputs) == "<INPUT>...");
    const auto vector_input_help = deco::desc::from_deco_option(vector_opt.inputs, true);
    EXPECT_TRUE(vector_input_help.find("<INPUT>...") != std::string::npos);
    EXPECT_TRUE(vector_input_help.find("Input files") != std::string::npos);
}

TEST_CASE(from_deco_option_uses_configured_help_layout_and_default_help) {
    DescOpt opt{};
    NoHelpDescOpt no_help_opt{};
    auto config = deco::config::get();
    config.render.compatible.usage.help_column = 10;
    config.render.compatible.usage.default_help = "configured help text";

    const auto verbose_help = deco::desc::from_deco_option(opt.verbose, true, {}, &config);
    EXPECT_TRUE(verbose_help.find("-v, --verbose") != std::string::npos);
    EXPECT_TRUE(verbose_help.find("\n") != std::string::npos);
    EXPECT_TRUE(verbose_help.find("Show version and exit") != std::string::npos);

    const auto no_help = deco::desc::from_deco_option(no_help_opt.no_help_flag, true, {}, &config);
    EXPECT_TRUE(no_help.find("configured help text") != std::string::npos);
}

TEST_CASE(from_deco_option_uses_override_config_for_non_option_help) {
    NoHelpDescOpt no_help_opt{};
    auto config = deco::config::get();
    config.render.compatible.usage.default_help = "fallback from override";

    const auto no_help = deco::desc::from_deco_option(no_help_opt.no_help_flag, true, {}, &config);
    EXPECT_TRUE(no_help.find("fallback from override") != std::string::npos);
}

TEST_CASE(from_deco_option_infers_enum_meta_var_for_vector_results) {
    EnumVectorDescOpt opt{};

    EXPECT_TRUE(deco::desc::from_deco_option(opt.values, false, "values") ==
                "--values,<alpha|beta|gamma>[,<alpha|beta|gamma>...]");
    EXPECT_TRUE(deco::desc::from_deco_option(opt.pair, false, "pair") ==
                "--pair <alpha|beta|gamma> <alpha|beta|gamma>");
    EXPECT_TRUE(deco::desc::from_deco_option(opt.inputs) == "<alpha|beta|gamma>...");
}

};  // TEST_SUITE(deco_descriptor)
