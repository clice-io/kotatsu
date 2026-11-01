// Exclusive fields: a variant of groups of the command's own arguments, the one the argv gives
// an argument of first held, and an argument of another a conflict.

#include <expected>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "cli/harness/argv.h"
#include "kotatsu/cli/cli.h"
#include "kotatsu/zest/zest.h"

namespace kotatsu::cli {

namespace {

KANNOTATE_TYPE(struct ByLocator, description = "by locator");

struct ByLocator {
    KANNOTATE(&locator, cli::positional = true, cli::meta_var = "<LOCATOR>")
    std::string locator;
    KANNOTATE(&new_name, cli::positional = true, cli::meta_var = "<NEW-NAME>")
    std::string new_name;
};

KANNOTATE_TYPE(struct BySpec, description = "by spec");

struct BySpec {
    KANNOTATE(&spec, presence = required)
    std::string spec;
    bool dry = false;
};

struct Rename {
    bool verbose = false;
    KANNOTATE(&target, cli::exclusive = true, description = "Target")
    std::variant<ByLocator, BySpec> target;
};

struct Pick {
    KANNOTATE(&target, cli::exclusive = true, presence = required)
    std::variant<BySpec, ByLocator> target;
};

struct Maybe {
    KANNOTATE(&target, cli::exclusive = true)
    std::optional<std::variant<BySpec, ByLocator>> target;
};

struct Wrapped {
    int level = 0;
    KANNOTATE(&target, cli::exclusive = true, presence = required)
    std::variant<BySpec, ByLocator> target;
};

struct Outer {
    std::optional<Wrapped> wrapped;
};

struct WithRest {
    KANNOTATE(&command, cli::rest = true)
    std::vector<std::string> command;
};

struct Pass {
    KANNOTATE(&how, cli::exclusive = true)
    std::variant<WithRest, BySpec> how;
};

/// The rest in an alternative other than the first, which "--" alone picks.
struct RestLast {
    KANNOTATE(&how, cli::exclusive = true)
    std::variant<BySpec, WithRest> how;
};

struct ByName {
    std::string name;
};

struct ById {
    int id = 0;
};

struct Plain {
    KANNOTATE(&choice, cli::exclusive = true)
    std::variant<ByName, ById> choice;
};

template <typename T>
std::expected<T, Error> parse_line(std::string_view line) {
    return cli::parse<T>(line.empty() ? std::vector<std::string>{} : test::split(line));
}

ZEST_SUITE(cli_parse_exclusive) {

ZEST_CASE(option_picks_its_alternative) {
    auto rename = parse_line<Rename>("--spec s --dry");
    ZASSERT(rename.has_value());
    ZASSERT(rename->target.index() == 1U);
    ZEXPECT(std::string(std::get<BySpec>(rename->target).spec) == "s");
    ZEXPECT(std::get<BySpec>(rename->target).dry);
}

ZEST_CASE(positional_picks_its_alternative) {
    auto rename = parse_line<Rename>("old new --verbose");
    ZASSERT(rename.has_value());
    ZASSERT(rename->target.index() == 0U);
    ZEXPECT(std::string(std::get<ByLocator>(rename->target).new_name) == "new");
    ZEXPECT(rename->verbose);
}

ZEST_CASE(option_of_another_alternative_fails) {
    auto rename = parse_line<Rename>("old new --spec s");
    ZASSERT(!rename.has_value());
    ZEXPECT(rename.error().kind == Error::Kind::Conflict);
    ZEXPECT(rename.error().message == "--spec <value> cannot be used with <LOCATOR>");
    ZEXPECT(rename.error().begin == 2U);
}

ZEST_CASE(positional_of_another_alternative_fails) {
    auto rename = parse_line<Rename>("--spec s old");
    ZASSERT(!rename.has_value());
    ZEXPECT(rename.error().kind == Error::Kind::Conflict);
    ZEXPECT(rename.error().message == "<LOCATOR> cannot be used with --spec <value>");
    ZEXPECT(rename.error().begin == 2U);
}

ZEST_CASE(required_option_of_the_held_alternative_left_out_fails) {
    auto rename = parse_line<Rename>("--dry");
    ZASSERT(!rename.has_value());
    ZEXPECT(rename.error().kind == Error::Kind::MissingRequired);
    ZEXPECT(rename.error().message == "required option --spec <value> is missing");
}

ZEST_CASE(nothing_given_keeps_the_initial_alternative) {
    auto rename = parse_line<Rename>("--verbose");
    ZASSERT(rename.has_value());
    ZEXPECT(rename->target.index() == 0U);
}

ZEST_CASE(required_field_left_out_fails) {
    auto pick = parse_line<Pick>("");
    ZASSERT(!pick.has_value());
    ZEXPECT(pick.error().kind == Error::Kind::MissingRequired);
    ZEXPECT(pick.error().message == "one of --spec <value> or <LOCATOR> is required");
}

ZEST_CASE(optional_field_left_out_is_empty) {
    auto maybe = parse_line<Maybe>("");
    ZASSERT(maybe.has_value());
    ZEXPECT(!maybe->target.has_value());
}

ZEST_CASE(held_alternative_writes_alone) {
    Rename rename;
    rename.target = BySpec{.spec = std::string("s"), .dry = true};
    ZEXPECT(cli::to_argv(rename) == (std::vector<std::string>{"--spec=s", "--dry"}));
}

ZEST_CASE(written_alternative_parses_back) {
    Rename rename;
    rename.target = BySpec{.spec = std::string("s"), .dry = true};
    auto back = cli::parse<Rename>(cli::to_argv(rename));
    ZASSERT(back.has_value());
    ZEXPECT(back->target.index() == 1U);
}

ZEST_CASE(held_positional_alternative_writes_its_values) {
    Rename rename;
    rename.target = ByLocator{.locator = std::string("old"), .new_name = std::string("-new")};
    ZEXPECT(cli::to_argv(rename) == (std::vector<std::string>{"--", "old", "-new"}));
}

ZEST_CASE(given_paths_go_through_the_field) {
    std::vector<std::string> paths;
    auto rename =
        cli::parse<Rename>(test::split("--spec s --dry"),
                           [&](Rename&, const Given& given) { paths.push_back(given.path); });
    ZASSERT(rename.has_value());
    ZEXPECT(paths == (std::vector<std::string>{"target.spec", "target.dry"}));
}

ZEST_CASE(optional_field_given_holds_its_alternative) {
    auto maybe = parse_line<Maybe>("--spec s");
    ZASSERT(maybe.has_value());
    ZASSERT(maybe->target.has_value());
    ZEXPECT(maybe->target->index() == 0U);
}

ZEST_CASE(field_in_an_optional_group_left_out_with_it_passes) {
    ZEXPECT(parse_line<Outer>("").has_value());
}

ZEST_CASE(optional_group_given_without_its_required_field_fails) {
    auto outer = parse_line<Outer>("--level 1");
    ZASSERT(!outer.has_value());
    ZEXPECT(outer.error().message == "one of --spec <value> or <LOCATOR> is required");
}

ZEST_CASE(rest_of_another_alternative_fails) {
    auto pass = parse_line<Pass>("--spec s -- x");
    ZASSERT(!pass.has_value());
    ZEXPECT(pass.error().kind == Error::Kind::Conflict);
}

ZEST_CASE(empty_rest_alternative_writes_its_delimiter) {
    auto rest = parse_line<RestLast>("--");
    ZASSERT(rest.has_value());
    ZEXPECT(rest->how.index() == 1U);
    ZEXPECT(cli::to_argv(*rest) == (std::vector<std::string>{"--"}));
}

ZEST_CASE(alternative_without_a_description_is_headed_by_its_name) {
    ZEXPECT(cli::usage<Plain>("plain").contains("choice, one of: by-name:"));
}

ZEST_CASE(usage_heads_each_alternative) {
    const auto usage = cli::usage<Rename>("rename");
    ZEXPECT(usage.contains("Target, one of: by locator:\n  <LOCATOR>"));
    ZEXPECT(usage.contains("Target, one of: by spec:\n  --spec <value>"));
}

};  // ZEST_SUITE(cli_parse_exclusive)

}  // namespace

}  // namespace kotatsu::cli
