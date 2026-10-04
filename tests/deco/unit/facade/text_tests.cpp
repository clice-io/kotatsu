#include <string>
#include <vector>

#include "deco/harness/argv.h"
#include "deco/harness/text.h"
#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco::cli::text {

namespace {

UsageDocument usage_document() {
    return UsageDocument{
        .overview = "app [OPTIONS]",
        .groups = {UsageGroup{
            .title = "<default>",
            .exclusive = false,
            .is_default = true,
            .entries = {UsageEntry{.usage = "-v", .help = "verbose"}},
        }},
    };
}

SubCommandDocument subcommand_document() {
    return SubCommandDocument{
        .overview = "a tool",
        .usage_line = "app <command>",
        .has_usage_line = true,
        .entries = {SubCommandEntry{.name = "run", .description = "run it", .command = "run"}},
    };
}

ZEST_SUITE(deco_facade_text) {

ZEST_CASE(default_renderer_is_the_one_the_config_makes) {
    test::ScopedDefaultRenderer restore;
    clear_default_renderer();
    ZEXPECT(explicit_default_renderer() == nullptr);
    ZEXPECT(default_renderer().style.usage.options_heading ==
            config::get().render.compatible.usage.options_heading);
}

ZEST_CASE(set_default_renderer_takes_over) {
    test::ScopedDefaultRenderer restore;
    set_default_renderer(ModernRenderer());
    ZASSERT(explicit_default_renderer() != nullptr);
    ZEXPECT(default_renderer().style.usage.options_heading == "Options");
    clear_default_renderer();
    ZEXPECT(default_renderer().style.usage.options_heading == "Options:");
}

ZEST_CASE(resolve_renderer_prefers_the_one_given) {
    const auto renderer = test::tagged_renderer();
    ZEXPECT(&resolve_renderer(&renderer) == &renderer);
    ZEXPECT(&resolve_renderer(nullptr) == &default_renderer());
}

ZEST_CASE(render_calls_the_renderers_own_functions) {
    const auto renderer = test::tagged_renderer();
    ZEXPECT(render_usage(usage_document(), false, &renderer) == "USAGE<app [OPTIONS]:plain>");
    ZEXPECT(render_subcommands(subcommand_document(), &renderer) == "SUB<app <command>:1>");
    ZEXPECT(render_diagnostic(diagnostic_message("bad"), &renderer) == "ERR<0:bad>");
}

ZEST_CASE(renderer_without_functions_renders_compatibly) {
    const Renderer bare;
    const CompatibleRenderer compatible;
    const auto argv = test::args("-x", "y");
    ZEXPECT(render_usage(usage_document(), true, &bare) ==
            render_usage(usage_document(), true, &compatible));
    ZEXPECT(render_subcommands(subcommand_document(), &bare) ==
            render_subcommands(subcommand_document(), &compatible));
    ZEXPECT(render_diagnostic(diagnostic_at(argv, 0, 1, "bad"), &bare) ==
            render_diagnostic(diagnostic_at(argv, 0, 1, "bad"), &compatible));
}

ZEST_CASE(renderers_take_their_config) {
    CompatibleRendererConfig config;
    config.usage.help_column = 7;
    ZEXPECT(CompatibleRenderer(config).style.usage.help_column == 7U);

    ModernRendererConfig modern;
    modern.diagnostic.pointer = '*';
    ZEXPECT(ModernRenderer(modern).style.diagnostic.pointer == '*');
}

ZEST_CASE(diagnostic_at_is_positioned) {
    const auto argv = test::args("a", "b");
    const auto diagnostic = diagnostic_at(argv, 1, 2, "bad");
    ZEXPECT(diagnostic.positioned);
    ZEXPECT(diagnostic.argv.data() == argv.data());
    ZEXPECT(diagnostic.begin == 1U);
    ZEXPECT(diagnostic.end == 2U);
    ZEXPECT(diagnostic.message == "bad");
}

ZEST_CASE(diagnostic_message_has_no_position) {
    const auto diagnostic = diagnostic_message("bad");
    ZEXPECT(!diagnostic.positioned);
    ZEXPECT(diagnostic.argv.empty());
}

ZEST_CASE(usage_entry_pads_the_usage_to_the_help_column) {
    UsageStyle style;
    style.help_column = 6;
    ZEXPECT(render_usage_entry({.usage = "-v", .help = "verbose"}, style) == "  -v    verbose");
}

ZEST_CASE(usage_entry_reaching_the_help_column_puts_the_help_below) {
    UsageStyle style;
    style.help_column = 4;
    ZEXPECT(render_usage_entry({.usage = "-o <FILE>", .help = "output"}, style) ==
            "  -o <FILE>\n      output");
    ZEXPECT(render_usage_entry({.usage = "-ab", .help = "x"}, style) == "  -ab x");
    ZEXPECT(render_usage_entry({.usage = "-abc", .help = "x"}, style) == "  -abc\n      x");
}

ZEST_CASE(usage_entry_without_help_shows_the_default) {
    UsageStyle style;
    style.help_column = 4;
    style.default_help = "(none)";
    ZEXPECT(render_usage_entry({.usage = "-v", .help = ""}, style) == "  -v  (none)");
}

};  // ZEST_SUITE(deco_facade_text)

}  // namespace

}  // namespace kota::deco::cli::text
