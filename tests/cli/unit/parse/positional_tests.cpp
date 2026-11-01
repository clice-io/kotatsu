// Positional arguments, filled in declaration order, and the arguments after "--".

#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "cli/harness/argv.h"
#include "kotatsu/cli/cli.h"
#include "kotatsu/zest/zest.h"

namespace kotatsu::cli {

namespace {

struct Copy {
    KANNOTATE(&from, cli::positional = true, cli::meta_var = "<FROM>")
    std::string from;
    KANNOTATE(&to, cli::positional = true, cli::meta_var = "<TO>")
    std::string to;
    bool force = false;
};

struct Format {
    KANNOTATE(&paths, cli::positional = true)
    std::vector<std::string> paths;
};

struct Pair {
    KANNOTATE(&pair, cli::positional = true, cli::arg_num = 2)
    std::vector<std::string> pair;
};

struct Search {
    KANNOTATE(&locator, cli::positional = true)
    std::optional<std::string> locator;
};

struct Few {
    bool k = false;
    KANNOTATE(&words, cli::positional = true, presence = required, cli::arg_num = ArgNum(1, 3))
    std::vector<std::string> words;
};

struct Toggle {
    KANNOTATE(&on, cli::positional = true)
    bool on = true;
};

struct Run {
    std::optional<std::string> from;
    KANNOTATE(&command, cli::rest = true)
    std::vector<std::string> command;
};

template <typename T>
std::expected<T, Error> parse_line(std::string_view line) {
    return cli::parse<T>(line.empty() ? std::vector<std::string>{} : test::split(line));
}

ZEST_SUITE(cli_parse_positional) {

ZEST_CASE(positionals_fill_in_declaration_order) {
    auto copy = parse_line<Copy>("a --force b");
    ZASSERT(copy.has_value());
    ZEXPECT(std::string(copy->from) == "a");
    ZEXPECT(std::string(copy->to) == "b");
    ZEXPECT(copy->force);
}

ZEST_CASE(scalar_positional_left_out_fails) {
    auto copy = parse_line<Copy>("a");
    ZASSERT(!copy.has_value());
    ZEXPECT(copy.error().kind == Error::Kind::MissingRequired);
    ZEXPECT(copy.error().message == "expected 1 input argument, got 0");
    // At the end of the argv.
    ZEXPECT(copy.error().begin == 1U);
}

ZEST_CASE(one_positional_too_many_fails) {
    auto copy = parse_line<Copy>("a b c");
    ZASSERT(!copy.has_value());
    ZEXPECT(copy.error().kind == Error::Kind::UnexpectedArgument);
    ZEXPECT(copy.error().message == "unexpected input argument c: expected 2 input arguments");
    ZEXPECT(copy.error().begin == 2U);
}

ZEST_CASE(positional_without_a_field_fails) {
    auto run = parse_line<Run>("a");
    ZASSERT(!run.has_value());
    ZEXPECT(run.error().message == "unexpected input argument a");
}

ZEST_CASE(list_takes_any_number) {
    auto none = parse_line<Format>("");
    ZASSERT(none.has_value());
    ZEXPECT(none->paths.empty());
    auto three = parse_line<Format>("a b c");
    ZASSERT(three.has_value());
    ZEXPECT(three->paths == (std::vector<std::string>{"a", "b", "c"}));
}

ZEST_CASE(counted_list_takes_its_count) {
    auto pair = parse_line<Pair>("a b");
    ZASSERT(pair.has_value());
    ZEXPECT(pair->pair == (std::vector<std::string>{"a", "b"}));
}

ZEST_CASE(counted_list_may_be_left_out) {
    ZEXPECT(parse_line<Pair>("").has_value());
}

ZEST_CASE(counted_list_short_of_its_count_fails) {
    auto one = parse_line<Pair>("a");
    ZASSERT(!one.has_value());
    ZEXPECT(one.error().message == "expected 2 input arguments, got 1");
}

ZEST_CASE(counted_list_past_its_count_fails) {
    auto three = parse_line<Pair>("a b c");
    ZASSERT(!three.has_value());
    ZEXPECT(three.error().message == "unexpected input argument c: expected 2 input arguments");
}

ZEST_CASE(ranged_list_says_its_range) {
    auto none = parse_line<Few>("-k");
    ZASSERT(!none.has_value());
    ZEXPECT(none.error().message == "expected 1 to 3 input arguments, got 0");
}

ZEST_CASE(optional_positional_may_be_left_out) {
    auto none = parse_line<Search>("");
    ZASSERT(none.has_value());
    ZEXPECT(!none->locator.has_value());
    auto one = parse_line<Search>("main");
    ZASSERT(one.has_value());
    ZEXPECT(one->locator == std::optional<std::string>("main"));
}

ZEST_CASE(empty_argument_is_a_positional) {
    auto search = cli::parse<Search>({""});
    ZASSERT(search.has_value());
    ZEXPECT(search->locator == std::optional<std::string>(""));
}

ZEST_CASE(rest_takes_everything_after_dash_dash) {
    auto run = parse_line<Run>("--from x -- make -j --from y");
    ZASSERT(run.has_value());
    ZEXPECT(run->from == std::optional<std::string>("x"));
    ZEXPECT(run->command == (std::vector<std::string>{"make", "-j", "--from", "y"}));
}

ZEST_CASE(dash_dash_ends_the_options) {
    auto copy = parse_line<Copy>("-- -a --force");
    ZASSERT(copy.has_value());
    ZEXPECT(std::string(copy->from) == "-a");
    ZEXPECT(std::string(copy->to) == "--force");
    ZEXPECT(!copy->force);
}

ZEST_CASE(positional_bool_reads_its_value) {
    auto toggle = parse_line<Toggle>("false");
    ZASSERT(toggle.has_value());
    ZEXPECT(!toggle->on);
}

};  // ZEST_SUITE(cli_parse_positional)

}  // namespace

}  // namespace kotatsu::cli
