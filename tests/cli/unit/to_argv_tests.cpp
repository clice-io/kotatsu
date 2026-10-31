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
    ZEXPECT(cli::to_argv(Run{}).empty());
}

};  // ZEST_SUITE(cli_to_argv)

}  // namespace

}  // namespace kotatsu::cli
