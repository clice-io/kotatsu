#include <string>

#include "deco/harness/text.h"
#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco::cli::text {

namespace {

SubCommandDocument listing() {
    return SubCommandDocument{
        .overview = "A build tool",
        .usage_line = "tool [OPTIONS] <command>",
        .has_usage_line = true,
        .entries =
            {
                      SubCommandEntry{.name = "run", .description = "Run a target", .command = "run"},
                      SubCommandEntry{.name = "inspect",
                                .description = "Show metadata",
                                .command = "show"},
                      SubCommandEntry{.name = "x", .description = "", .command = "x"},
                      },
    };
}

/// `document` as the compatible and the modern renderer lay it out, each with `style`.
void expect_laid_out(const SubCommandDocument& document, const SubCommandStyle& style = {}) {
    CompatibleRendererConfig compatible;
    compatible.subcommand = style;
    const CompatibleRenderer compatible_renderer(compatible);
    EXPECT_SNAPSHOT(render_subcommands(document, &compatible_renderer), "compatible");

    ModernRendererConfig modern;
    modern.subcommand = style;
    modern.subcommand.heading = "Commands";
    const ModernRenderer modern_renderer(modern);
    EXPECT_SNAPSHOT(test::visible(render_subcommands(document, &modern_renderer)), "modern");
}

ZEST_SUITE(deco_facade_text_subcommand) {

ZEST_CASE(commands_are_listed_aligned) {
    expect_laid_out(listing());
}

ZEST_CASE(usage_line_shows_only_with_a_default) {
    auto document = listing();
    document.has_usage_line = false;
    expect_laid_out(document);
}

ZEST_CASE(no_commands_leaves_the_usage_line) {
    auto document = listing();
    document.entries.clear();
    expect_laid_out(document);
}

ZEST_CASE(style_turns_parts_off) {
    SubCommandStyle style;
    style.show_overview = false;
    style.show_usage_line = false;
    style.show_description = false;
    style.show_command_alias = false;
    expect_laid_out(listing(), style);
}

ZEST_CASE(descriptions_can_follow_their_names) {
    SubCommandStyle style;
    style.align_description = false;
    expect_laid_out(listing(), style);
}

};  // ZEST_SUITE(deco_facade_text_subcommand)

}  // namespace

}  // namespace kota::deco::cli::text
