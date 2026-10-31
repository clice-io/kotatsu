// Options of every shape: what each reads, what it keeps when not given, and how a value it
// cannot take fails.

#include <cstdint>
#include <expected>
#include <optional>
#include <ranges>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "cli/harness/argv.h"
#include "kotatsu/cli/cli.h"
#include "kotatsu/zest/zest.h"

namespace kotatsu::cli {

namespace {

enum class Level : std::uint8_t { Trace, Debug, Info, Warn };

struct Options {
    bool verbose = false;
    KOTATSU_ANNOTATE(cli::count = true, cli::names = {"-v"})
    <std::uint32_t> noise = 0;
    int jobs = 1;
    double ratio = 0.5;
    std::string name;
    Level level = Level::Warn;
    std::optional<std::string> output;
    std::vector<std::string> include;
    KOTATSU_ANNOTATE(cli::comma = true)
    <std::vector<std::string>> tags;
    std::vector<std::pair<std::string, std::string>> define;
    std::tuple<int, int> size = {0, 0};
    KOTATSU_ANNOTATE(cli::names = {"-o", "--out"})
    <std::string> out;
    KOTATSU_ANNOTATE(cli::names = {"-I"}, cli::style = KVStyle::Joined)
    <std::string> dir;
    KOTATSU_ANNOTATE(cli::names = {"--sep"}, cli::style = KVStyle::Separate)
    <std::string> sep;
    KOTATSU_ANNOTATE(cli::required = true)
    <std::string> must = "x";
    std::string x;
};

template <typename T = Options>
std::expected<T, Error> parse_line(std::string_view line) {
    return cli::parse<T>(test::split(line));
}

/// The options of an argv that must parse.
Options parsed(std::string_view line) {
    auto options = parse_line(line);
    ZASSERT(options.has_value());
    return std::move(*options);
}

ZEST_SUITE(cli_parse_options) {

ZEST_CASE(nothing_given_keeps_every_default) {
    auto options = parse_line("--must y");
    ZASSERT(options.has_value());
    ZEXPECT(!options->verbose);
    ZEXPECT(options->jobs == 1);
    ZEXPECT(options->level == Level::Warn);
    ZEXPECT(!options->output.has_value());
    ZEXPECT(options->include.empty());
}

ZEST_CASE(field_names_become_option_names) {
    auto options = parsed("--must y --verbose --jobs 4 --name n -x short");
    ZEXPECT(options.verbose);
    ZEXPECT(options.jobs == 4);
    ZEXPECT(std::string(options.name) == "n");
    ZEXPECT(std::string(options.x) == "short");
}

ZEST_CASE(value_takes_it_separate_or_after_equals) {
    ZEXPECT(parsed("--must y --jobs=7").jobs == 7);
    ZEXPECT(parsed("--must y --jobs 7").jobs == 7);
}

ZEST_CASE(scalar_keeps_its_last_value) {
    ZEXPECT(parsed("--must y --jobs 1 --jobs 2").jobs == 2);
}

ZEST_CASE(counting_flag_counts) {
    ZEXPECT(parsed("--must y -v -v -v").noise == 3U);
}

ZEST_CASE(numbers_read_in_their_type) {
    auto options = parsed("--must y --ratio 2.5 --jobs -3");
    ZEXPECT(options.ratio == 2.5);
    ZEXPECT(options.jobs == -3);
}

ZEST_CASE(enum_reads_any_spelling_of_its_names) {
    ZEXPECT(parsed("--must y --level info").level == Level::Info);
    ZEXPECT(parsed("--must y --level Debug").level == Level::Debug);
}

ZEST_CASE(optional_given_holds_its_value) {
    ZEXPECT(parsed("--must y --output a.out").output == std::optional<std::string>("a.out"));
}

ZEST_CASE(list_collects_every_occurrence) {
    auto options = parsed("--must y --include a --include=b");
    ZEXPECT(options.include == (std::vector<std::string>{"a", "b"}));
}

ZEST_CASE(comma_list_takes_its_values_after_its_comma) {
    auto options = parsed("--must y --tags,a,b --tags,c");
    ZEXPECT(options.tags == (std::vector<std::string>{"a", "b", "c"}));
}

ZEST_CASE(comma_list_without_its_comma_is_unknown) {
    auto options = parse_line("--must y --tags=a");
    ZASSERT(!options.has_value());
    ZEXPECT(options.error().kind == Error::Kind::UnknownOption);
}

ZEST_CASE(tuple_takes_a_value_for_each_element) {
    auto options = parsed("--must y --size 3 4 --define A 1 --define B 2");
    ZEXPECT(options.size == (std::tuple<int, int>{3, 4}));
    ZASSERT(options.define.size() == 2U);
    ZEXPECT(options.define[1] == (std::pair<std::string, std::string>{"B", "2"}));
}

ZEST_CASE(explicit_names_replace_the_field_name) {
    ZEXPECT(std::string(parsed("--must y -o a").out) == "a");
    ZEXPECT(std::string(parsed("--must y --out=b").out) == "b");
    ZEXPECT(!parse_line("--must y --out-file a").has_value());
}

ZEST_CASE(joined_style_takes_its_value_joined) {
    ZEXPECT(std::string(parsed("--must y -Iinclude").dir) == "include");
}

ZEST_CASE(separate_style_takes_no_value_after_equals) {
    ZEXPECT(std::string(parsed("--must y --sep v").sep) == "v");
    auto options = parse_line("--must y --sep=v");
    ZASSERT(!options.has_value());
    ZEXPECT(options.error().kind == Error::Kind::UnknownOption);
}

ZEST_CASE(empty_value_is_a_value) {
    ZEXPECT(std::string(parsed("--must y --name ").name).empty());
    auto options = cli::parse<Options>({"--must", "y", "--name", ""});
    ZASSERT(options.has_value());
    ZEXPECT(std::string(options->name).empty());
}

ZEST_CASE(required_option_missing_fails) {
    auto options = parse_line("--jobs 2");
    ZASSERT(!options.has_value());
    ZEXPECT(options.error().kind == Error::Kind::MissingRequired);
    ZEXPECT(options.error().message == "required option --must <value> is missing");
}

ZEST_CASE(unknown_option_fails) {
    auto options = parse_line("--must y --bogus");
    ZASSERT(!options.has_value());
    ZEXPECT(options.error().kind == Error::Kind::UnknownOption);
    ZEXPECT(options.error().message == "unknown option '--bogus'");
    ZEXPECT(options.error().begin == 2U);
    ZEXPECT(options.error().end == 3U);
}

ZEST_CASE(missing_value_fails) {
    auto options = parse_line("--must y --jobs");
    ZASSERT(!options.has_value());
    ZEXPECT(options.error().kind == Error::Kind::MissingValue);
    ZEXPECT(options.error().message == "missing argument value");
}

ZEST_CASE(value_of_the_wrong_type_fails_at_the_value) {
    auto options = parse_line("--must y --jobs many");
    ZASSERT(!options.has_value());
    ZEXPECT(options.error().kind == Error::Kind::InvalidValue);
    ZEXPECT(options.error().message == "invalid integer value: many");
    ZEXPECT(options.error().begin == 3U);
    ZEXPECT(options.error().end == 4U);
}

ZEST_CASE(value_joined_to_its_name_fails_at_the_argument) {
    auto options = parse_line("--must y --jobs=many");
    ZASSERT(!options.has_value());
    ZEXPECT(options.error().begin == 2U);
    ZEXPECT(options.error().end == 3U);
}

ZEST_CASE(enum_of_another_name_fails) {
    auto options = parse_line("--must y --level loud");
    ZASSERT(!options.has_value());
    ZEXPECT(options.error().message ==
            "invalid enum value: loud (supported: trace, debug, info, warn)");
}

ZEST_CASE(other_ranges_of_strings_parse) {
    const std::vector<std::string> words = {"must", "y"};
    auto options = cli::parse<Options>(words | std::views::transform([](const std::string& w) {
                                           return w == "must" ? std::string("--must") : w;
                                       }));
    ZASSERT(options.has_value());
    ZEXPECT(std::string(options->must) == "y");
}

};  // ZEST_SUITE(cli_parse_options)

}  // namespace

}  // namespace kotatsu::cli
