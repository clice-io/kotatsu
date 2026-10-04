#include <string>

#include "deco/harness/text.h"
#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco::cli::text {

namespace {

UsageGroup default_group() {
    return UsageGroup{
        .title = "<default> (the default category for options)",
        .exclusive = false,
        .is_default = true,
        .entries =
            {
                      UsageEntry{.usage = "-v|--verbose", .help = "Show more"},
                      UsageEntry{.usage = "<FILE>", .help = ""},
                      },
    };
}

UsageDocument grouped() {
    return UsageDocument{
        .overview = "app [OPTIONS] <FILE>",
        .groups =
            {
                     default_group(),
                     UsageGroup{
                    .title = "<mode> (one mode at a time)",
                    .exclusive = true,
                    .is_default = false,
                    .entries =
                        {
                            UsageEntry{.usage = "--list", .help = "List them"},
                            UsageEntry{.usage = "--a-long-option-name-indeed <VALUE>",
                                       .help = "Reaches the help column"},
                        },
                }, },
    };
}

/// `document` as the compatible and the modern renderer lay it out, with `config` for both.
void expect_laid_out(const UsageDocument& document,
                     bool include_help = true,
                     const CompatibleRendererConfig& compatible = {},
                     const ModernRendererConfig& modern = {}) {
    const CompatibleRenderer compatible_renderer(compatible);
    ZEXPECT(
        zest::snapshot(render_usage(document, include_help, &compatible_renderer), "compatible"));
    const ModernRenderer modern_renderer(modern);
    ZEXPECT(zest::snapshot(test::visible(render_usage(document, include_help, &modern_renderer)),
                           "modern"));
}

ZEST_SUITE(deco_facade_text_usage) {

ZEST_CASE(groups_are_headed) {
    expect_laid_out(grouped());
}

ZEST_CASE(one_default_group_is_not_headed) {
    expect_laid_out(UsageDocument{.overview = "app", .groups = {default_group()}});
}

ZEST_CASE(grouping_can_be_turned_off) {
    CompatibleRendererConfig compatible;
    compatible.usage.group_by_category = false;
    ModernRendererConfig modern;
    modern.usage.group_by_category = false;
    expect_laid_out(grouped(), true, compatible, modern);
}

ZEST_CASE(usages_can_be_listed_alone) {
    expect_laid_out(grouped(), false);
}

ZEST_CASE(style_names_the_headings) {
    CompatibleRendererConfig compatible;
    compatible.usage.options_heading = "Flags:";
    compatible.usage.group_prefix = "Section ";
    compatible.usage.exclusive_suffix = " (pick one)";
    compatible.usage.default_help = "?";
    compatible.usage.help_column = 16;
    ModernRendererConfig modern;
    modern.usage.options_heading = "Flags";
    modern.usage.exclusive_suffix = " (pick one)";
    modern.usage.default_help = "?";
    modern.usage.help_column = 16;
    expect_laid_out(grouped(), true, compatible, modern);
}

};  // ZEST_SUITE(deco_facade_text_usage)

}  // namespace

}  // namespace kota::deco::cli::text
