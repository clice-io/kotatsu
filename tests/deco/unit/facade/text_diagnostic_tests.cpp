#include <string>
#include <vector>

#include "deco/harness/argv.h"
#include "deco/harness/text.h"
#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco::cli::text {

namespace {

/// `diagnostic` as the compatible and the modern renderer lay it out, each with `style`.
void expect_laid_out(const Diagnostic& diagnostic, const PositionStyle& style = {}) {
    CompatibleRendererConfig compatible;
    compatible.diagnostic = style;
    const CompatibleRenderer compatible_renderer(compatible);
    EXPECT_SNAPSHOT(render_diagnostic(diagnostic, &compatible_renderer), "compatible");

    ModernRendererConfig modern;
    modern.diagnostic = style;
    const ModernRenderer modern_renderer(modern);
    EXPECT_SNAPSHOT(test::visible(render_diagnostic(diagnostic, &modern_renderer)), "modern");
}

/// The lines of the compatible layout of `diagnostic` without its label: the source line,
/// the marker line and the message.
std::vector<std::string> unlabelled_lines(const Diagnostic& diagnostic, std::size_t width) {
    CompatibleRendererConfig config;
    config.diagnostic.show_label = false;
    config.diagnostic.max_source_width = width;
    const CompatibleRenderer renderer(config);
    return test::split(render_diagnostic(diagnostic, &renderer), '\n');
}

const std::vector<std::string> argv = test::args("app", "--nope", "x");

/// An argv too long for one line: a short option, then two long values.
const std::vector<std::string> long_argv =
    test::args("-s", std::string(200, 'a'), std::string(220, 'b'), "--", "make");

ZEST_SUITE(deco_facade_text_diagnostic) {

ZEST_CASE(argument_is_marked) {
    expect_laid_out(diagnostic_at(argv, 1, 2, "unknown option"));
}

ZEST_CASE(several_arguments_are_marked_as_one) {
    expect_laid_out(diagnostic_at(argv, 1, 3, "bad pair"));
}

ZEST_CASE(empty_range_marks_its_first_argument) {
    expect_laid_out(diagnostic_at(argv, 1, 1, "here"));
}

ZEST_CASE(end_of_argv_is_marked_past_it) {
    expect_laid_out(diagnostic_at(argv, 3, 3, "missing value"));
}

ZEST_CASE(empty_argv_is_labelled_at_its_end) {
    expect_laid_out(diagnostic_at({}, 0, 0, "nothing given"));
}

ZEST_CASE(message_without_position_stands_alone) {
    expect_laid_out(diagnostic_message("no position"));
}

ZEST_CASE(label_can_be_left_out) {
    PositionStyle style;
    style.show_label = false;
    expect_laid_out(diagnostic_at(argv, 1, 2, "unknown option"), style);
}

ZEST_CASE(source_line_can_be_left_out) {
    PositionStyle style;
    style.show_source_line = false;
    expect_laid_out(diagnostic_at(argv, 1, 2, "unknown option"), style);
}

ZEST_CASE(label_and_source_line_can_both_be_left_out) {
    PositionStyle style;
    style.show_label = false;
    style.show_source_line = false;
    expect_laid_out(diagnostic_at(argv, 1, 2, "unknown option"), style);
}

ZEST_CASE(positions_can_be_turned_off) {
    PositionStyle style;
    style.enabled = false;
    expect_laid_out(diagnostic_at(argv, 1, 2, "unknown option"), style);
}

ZEST_CASE(marker_characters_follow_the_style) {
    PositionStyle style;
    style.pointer = '>';
    style.underline = '-';
    expect_laid_out(diagnostic_at(argv, 1, 2, "unknown option"), style);
}

ZEST_CASE(long_line_is_cut_around_the_mark) {
    expect_laid_out(diagnostic_at(long_argv, 2, 3, "too long"));
}

ZEST_CASE(long_line_marked_at_its_end_keeps_its_tail) {
    expect_laid_out(diagnostic_at(long_argv, 5, 5, "missing value"));
}

ZEST_CASE(long_line_marked_at_its_start_keeps_its_head) {
    expect_laid_out(diagnostic_at(long_argv, 0, 1, "unknown option"));
}

ZEST_CASE(cut_line_fits_the_width) {
    for(const unsigned begin: {0U, 1U, 2U, 3U, 4U, 5U}) {
        for(const std::size_t width: {4U, 5U, 6U, 7U, 10U, 22U, 40U, 96U}) {
            ZEST_CONTEXT("argument {}, width {}", begin, width);
            const auto lines =
                unlabelled_lines(diagnostic_at(long_argv, begin, begin + 1, "x"), width);
            ASSERT(lines.size() == 3U);
            EXPECT(lines[0].size() <= width);
            // The marker is no wider than the line, save one past its end.
            EXPECT(lines[1].size() <= lines[0].size() + 1);
        }
    }
}

ZEST_CASE(cut_line_marks_the_argument) {
    // The marker stands under the start of the argument, or under the "..." that cuts it.
    const auto lines = unlabelled_lines(diagnostic_at(long_argv, 3, 4, "x"), 40);
    ASSERT(lines.size() == 3U);
    const auto marker = lines[1].find('^');
    ASSERT(marker != std::string::npos);
    EXPECT(zest::starts_with(std::string_view(lines[0]).substr(marker), "--"));
}

ZEST_CASE(narrow_width_leaves_part_of_an_ellipsis) {
    PositionStyle style;
    style.max_source_width = 2;
    expect_laid_out(diagnostic_at(long_argv, 1, 2, "too long"), style);
}

ZEST_CASE(zero_width_keeps_the_whole_line) {
    const auto lines = unlabelled_lines(diagnostic_at(long_argv, 2, 3, "x"), 0);
    ASSERT(lines.size() == 3U);
    EXPECT(lines[0].size() == 2U + 1 + 200 + 1 + 220 + 1 + 2 + 1 + 4);
}

ZEST_CASE(argv_is_viewed_not_copied) {
    auto words = test::args("--nope");
    const auto diagnostic = diagnostic_at(words, 0, 1, "unknown option");
    words[0] = "--renamed";
    const CompatibleRenderer renderer;
    EXPECT(zest::contains(render_diagnostic(diagnostic, &renderer), "--renamed"));
}

};  // ZEST_SUITE(deco_facade_text_diagnostic)

}  // namespace

}  // namespace kota::deco::cli::text
