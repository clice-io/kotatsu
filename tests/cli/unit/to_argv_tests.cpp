// Options written back as the argv that parses to them.

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "kotatsu/cli/cli.h"
#include "kotatsu/zest/zest.h"

namespace kotatsu::cli {

namespace {

enum class Level : std::uint8_t { Info, Warn };

struct Serve {
    int port = 0;
};

struct Options {
    bool verbose = false;
    KOTATSU_ANNOTATE(cli::count = true, cli::names = {"-v"})
    <std::uint32_t> noise = 0;
    KOTATSU_ANNOTATE(cli::names = {"-j", "--jobs"})
    <int> jobs = 0;
    Level level = Level::Info;
    std::optional<std::string> output;
    std::vector<std::string> include;
    KOTATSU_ANNOTATE(cli::comma = true)
    <std::vector<std::string>> tags;
    std::vector<std::pair<std::string, std::string>> define;
    KOTATSU_ANNOTATE(cli::names = {"-I"}, cli::style = KVStyle::Joined)
    <std::string> dir;
    KOTATSU_ANNOTATE(cli::names = {"--sep"}, cli::style = KVStyle::Separate)
    <std::string> sep;
    KOTATSU_ANNOTATE(cli::positional = true)
    <std::vector<std::string>> paths;
    std::optional<std::variant<Serve>> command;
};

struct Values {
    std::vector<bool> seen;
    KOTATSU_ANNOTATE(cli::positional = true)
    <bool> on = true;
    KOTATSU_ANNOTATE(cli::positional = true)
    <std::optional<std::string>> word;
};

struct Service {
    KOTATSU_ANNOTATE(tagged = true, tag_names = {"up"})
    <std::variant<Serve>> command;
};

struct Run {
    KOTATSU_ANNOTATE(cli::rest = true)
    <std::vector<std::string>> command;
};

ZEST_SUITE(cli_to_argv) {

ZEST_CASE(defaults_write_their_values) {
    ZEXPECT(cli::to_argv(Options{}) ==
            (std::vector<std::string>{"-j=0", "--level=info", "-I", "--sep", ""}));
}

ZEST_CASE(each_shape_writes_the_form_it_reads) {
    Options options;
    options.verbose = true;
    options.noise = 2u;
    options.jobs = 3;
    options.output = "a.out";
    options.include = std::vector<std::string>{"a", "b"};
    options.tags = std::vector<std::string>{"x", "y"};
    options.define = {
        {"N", "1"}
    };
    options.dir = std::string("inc");
    options.sep = std::string("s");
    options.paths = std::vector<std::string>{"f.cc"};
    options.command = Serve{.port = 8};
    ZEXPECT(cli::to_argv(options) == (std::vector<std::string>{"--verbose",
                                                               "-v",
                                                               "-v",
                                                               "-j=3",
                                                               "--level=info",
                                                               "--output=a.out",
                                                               "--include=a",
                                                               "--include=b",
                                                               "--tags,x,y",
                                                               "--define",
                                                               "N",
                                                               "1",
                                                               "-Iinc",
                                                               "--sep",
                                                               "s",
                                                               "f.cc",
                                                               "serve",
                                                               "--port=8"}));
}

ZEST_CASE(written_argv_parses_back) {
    Options options;
    options.jobs = -4;
    options.level = Level::Warn;
    options.include = std::vector<std::string>{"", "-x"};
    options.paths = std::vector<std::string>{"a", "b"};
    auto back = cli::parse<Options>(cli::to_argv(options));
    ZASSERT(back.has_value());
    ZEXPECT(back->jobs == -4);
    ZEXPECT(back->level == Level::Warn);
    ZEXPECT(back->include == options.include);
    ZEXPECT(back->paths == options.paths);
}

ZEST_CASE(rest_writes_after_dash_dash) {
    Run run;
    run.command = std::vector<std::string>{"make", "-j"};
    ZEXPECT(cli::to_argv(run) == (std::vector<std::string>{"--", "make", "-j"}));
}

ZEST_CASE(empty_rest_writes_nothing) {
    ZEXPECT(cli::to_argv(Run{}).empty());
}

ZEST_CASE(bool_values_write_their_text) {
    Values values;
    values.seen = {true, false};
    values.on = false;
    ZEXPECT(cli::to_argv(values) ==
            (std::vector<std::string>{"--seen=true", "--seen=false", "false"}));
    auto back = cli::parse<Values>(cli::to_argv(values));
    ZASSERT(back.has_value());
    ZEXPECT(back->seen == values.seen);
    ZEXPECT(!back->on);
}

ZEST_CASE(positional_taken_for_an_option_follows_dash_dash) {
    Values values;
    values.word = "-5";
    ZEXPECT(cli::to_argv(values) == (std::vector<std::string>{"--", "true", "-5"}));
    auto back = cli::parse<Values>(cli::to_argv(values));
    ZASSERT(back.has_value());
    ZEXPECT(back->word == std::optional<std::string>("-5"));
}

ZEST_CASE(tagged_subcommand_writes_its_tag_name) {
    Service service;
    service.command = Serve{.port = 3};
    ZEXPECT(cli::to_argv(service) == (std::vector<std::string>{"up", "--port=3"}));
}

};  // ZEST_SUITE(cli_to_argv)

}  // namespace

}  // namespace kotatsu::cli
