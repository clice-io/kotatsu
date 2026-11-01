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

/// An option named with '=' beside one whose '=' form it is, declared first.
struct Spelled {
    KOTATSU_ANNOTATE(cli::names = {"--x"})
    <std::string> separate;
    KOTATSU_ANNOTATE(cli::names = {"--x="})
    <std::string> joined;
};

struct Toggles {
    std::vector<bool> seen;
};

/// A command that names no option.
struct Files {
    KOTATSU_ANNOTATE(cli::positional = true)
    <std::vector<std::string>> files;
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

ZEST_CASE(comma_list_without_its_comma_fails) {
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

ZEST_CASE(explicit_names_are_the_option_s_names) {
    ZEXPECT(std::string(parsed("--must y -o a").out) == "a");
    ZEXPECT(std::string(parsed("--must y --out=b").out) == "b");
}

ZEST_CASE(field_name_beside_explicit_names_fails) {
    auto options = parse_line("--must y --out-file a");
    ZASSERT(!options.has_value());
    ZEXPECT(options.error().kind == Error::Kind::UnknownOption);
}

ZEST_CASE(joined_style_takes_its_value_joined) {
    ZEXPECT(std::string(parsed("--must y -Iinclude").dir) == "include");
}

ZEST_CASE(separate_style_takes_its_value_separate) {
    ZEXPECT(std::string(parsed("--must y --sep v").sep) == "v");
}

ZEST_CASE(separate_style_value_after_equals_fails) {
    auto options = parse_line("--must y --sep=v");
    ZASSERT(!options.has_value());
    ZEXPECT(options.error().kind == Error::Kind::UnknownOption);
}

ZEST_CASE(empty_value_is_a_value) {
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

ZEST_CASE(equals_form_is_left_to_the_option_named_so) {
    auto spelled = parse_line<Spelled>("--x=a --x b");
    ZASSERT(spelled.has_value());
    ZEXPECT(std::string(spelled->joined) == "a");
    ZEXPECT(std::string(spelled->separate) == "b");
}

ZEST_CASE(bool_value_reads_its_spellings) {
    auto toggles = parse_line<Toggles>("--seen true --seen off --seen YES");
    ZASSERT(toggles.has_value());
    ZEXPECT(toggles->seen == (std::vector<bool>{true, false, true}));
}

ZEST_CASE(bool_value_of_another_spelling_fails) {
    auto toggles = parse_line<Toggles>("--seen maybe");
    ZASSERT(!toggles.has_value());
    ZEXPECT(toggles.error().message == "invalid boolean value: maybe");
    ZEXPECT(toggles.error().begin == 1U);
}

ZEST_CASE(dash_word_in_a_command_without_options_fails) {
    auto files = parse_line<Files>("--force a");
    ZASSERT(!files.has_value());
    ZEXPECT(files.error().kind == Error::Kind::UnknownOption);
}

};  // ZEST_SUITE(cli_parse_options)

}  // namespace

}  // namespace kotatsu::cli
