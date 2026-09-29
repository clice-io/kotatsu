#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "deco/harness/argv.h"
#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco {

namespace {

using strings = std::vector<std::string>;

auto target_of(const ParsedArgOwning& arg) -> decl::AliasForwardResult {
    if(arg.values.front() == "bad") {
        return std::unexpected(std::string("no such target"));
    }
    return strings{"--target", arg.values.front()};
}

auto located_failure(const ParsedArgOwning&, const decl::IntoContext& context)
    -> decl::AliasForwardResult {
    return std::unexpected(context.format_error("cannot forward"));
}

enum class Support {
    Cpp,
    C,
    Cc,
};

struct Forwarded {
    DECO_CFG_START(required = false);

    DecoFlag(names = {"-v"};)
    verbose;

    DecoKV(names = {"--optimize"};)
    <std::string> optimize;

    DecoKV(names = {"--target"};)
    <std::string> target;

    DecoKV(names = {"--define"};)
    <std::string> define;

    DecoComma(names = {"--tags"};)
    <std::vector<std::string>> tags;

    DecoMulti(2, names = {"--pair"};)
    <std::vector<std::string>> pair;

    DecoKV(help = "support ext";)
    <Support> support_ext = Support::Cpp;

    DecoFlagAlias(names = {"-O1", "--optimize-one"}; forward = {"--optimize", "1"};
                  help = "Optimize a little";) _;

    DecoKVAlias(names = {"--define-alias"}; forward = {"--define"};) __;

    DecoKVAliasStyled(decl::KVStyle::Joined, names = {"-D"}; forward = {"--define"};) ___;

    DecoCommaAlias(names = {"--tags-alias"}; forward = {"--tags"};) ____;

    DecoCommaAlias(names = {"--verbose-tags"}; forward = {"-v", "--tags"};) _____;

    DecoCommaAlias(names = {"--nowhere"}; forward = std::vector<std::string_view>{};) ______;

    DecoMultiAlias(2, names = {"--pair-alias"}; forward = {"--pair"};) _______;

    DecoKVAlias(names = {"--target-alias"}; forward = target_of;) ________;

    DecoFlagAlias(names = {"--fails"}; forward = located_failure;) _________;

    DecoFlagAlias(names = {"--cc"}; forward = {"--support-ext", "cc"};) __________;

    DECO_CFG_END();
};

ZEST_SUITE(deco_facade_runtime_alias) {

ZEST_CASE(flag_alias_forwards_its_tokens) {
    auto argv = test::split("-O1");
    const auto parsed = cli::parse<Forwarded>(argv);
    ASSERT(parsed.has_value());
    EXPECT(parsed->options.optimize.as_optional() == std::optional<std::string>("1"));
}

ZEST_CASE(every_name_of_an_alias_forwards) {
    auto argv = test::split("--optimize-one");
    const auto parsed = cli::parse<Forwarded>(argv);
    ASSERT(parsed.has_value());
    EXPECT(parsed->options.optimize.as_optional() == std::optional<std::string>("1"));
}

ZEST_CASE(forward_goes_on_with_the_arguments_after_it) {
    auto argv = test::split("-v -O1 --target-alias dst");
    const auto parsed = cli::parse<Forwarded>(argv);
    ASSERT(parsed.has_value());
    EXPECT(parsed->options.verbose.as_optional() == std::optional(true));
    EXPECT(parsed->options.optimize.as_optional() == std::optional<std::string>("1"));
    EXPECT(parsed->options.target.as_optional() == std::optional<std::string>("dst"));
    // Parsing ends on the last forward's argv, without what came before the alias.
    EXPECT(parsed->argv().size() == 2U);
    EXPECT(parsed->next_cursor() == 2U);
    EXPECT(parsed->original_argv.size() == 4U);
}

ZEST_CASE(kv_alias_forwards_its_value) {
    auto argv = test::split("--define-alias NAME=1");
    const auto parsed = cli::parse<Forwarded>(argv);
    ASSERT(parsed.has_value());
    EXPECT(parsed->options.define.as_optional() == std::optional<std::string>("NAME=1"));
}

ZEST_CASE(joined_kv_alias_forwards_its_value) {
    auto argv = test::split("-DNAME=1");
    const auto parsed = cli::parse<Forwarded>(argv);
    ASSERT(parsed.has_value());
    EXPECT(parsed->options.define.as_optional() == std::optional<std::string>("NAME=1"));
}

ZEST_CASE(comma_alias_joins_its_values_to_the_last_token) {
    auto argv = test::split("--tags-alias,a,b --verbose-tags,c");
    const auto parsed = cli::parse<Forwarded>(argv);
    ASSERT(parsed.has_value());
    EXPECT(parsed->options.tags.as_optional() == std::optional(strings{"c"}));
    EXPECT(parsed->options.verbose.as_optional() == std::optional(true));
}

ZEST_CASE(comma_alias_without_tokens_fails) {
    auto argv = test::split("--nowhere,a");
    const auto parsed = cli::parse<Forwarded>(argv);
    ASSERT(!parsed.has_value());
    EXPECT(parsed.error().type == cli::ParseError::Type::IntoError);
    EXPECT(zest::starts_with(parsed.error().message, "at argv[0]:"));
    EXPECT(zest::ends_with(parsed.error().message,
                           "comma alias forward requires at least one target token"));
}

ZEST_CASE(multi_alias_forwards_its_values) {
    auto argv = test::split("--pair-alias left right");
    const auto parsed = cli::parse<Forwarded>(argv);
    ASSERT(parsed.has_value());
    EXPECT(parsed->options.pair.as_optional() == std::optional(strings{"left", "right"}));
}

ZEST_CASE(dynamic_forward_refusal_fails) {
    auto argv = test::split("-v --target-alias bad");
    const auto parsed = cli::parse<Forwarded>(argv);
    ASSERT(!parsed.has_value());
    EXPECT(parsed.error().type == cli::ParseError::Type::IntoError);
    EXPECT(zest::starts_with(parsed.error().message, "at argv[1]:"));
    EXPECT(zest::ends_with(parsed.error().message, "no such target"));
}

ZEST_CASE(dynamic_forward_with_context_refusal_fails) {
    auto argv = test::split("--fails");
    const auto parsed = cli::parse<Forwarded>(argv);
    ASSERT(!parsed.has_value());
    EXPECT(parsed.error().type == cli::ParseError::Type::IntoError);
    // Located once, by the forward.
    EXPECT(parsed.error().message == "at argv[0]:\n  --fails\n  ^~~~~~~\n  cannot forward");
}

ZEST_CASE(alias_forwards_to_an_enum_option) {
    auto argv = test::split("-O1 --cc");
    const auto parsed = cli::parse<Forwarded>(argv);
    ASSERT(parsed.has_value());
    EXPECT(parsed->options.support_ext.as_optional() == std::optional(Support::Cc));
}

ZEST_CASE(usage_shows_alias_names_not_fields) {
    std::ostringstream usage;
    cli::write_usage_for<Forwarded>(usage, "app [OPTIONS]");
    EXPECT(zest::contains(usage.str(), "-O1|--optimize-one"));
    EXPECT(zest::contains(usage.str(), "Optimize a little"));
    EXPECT(zest::contains(usage.str(), "--target-alias <value>"));
    EXPECT(zest::contains(usage.str(), "-D<value>"));
    EXPECT(!zest::contains(usage.str(), "__"));
}

};  // ZEST_SUITE(deco_facade_runtime_alias)

}  // namespace

}  // namespace kota::deco
