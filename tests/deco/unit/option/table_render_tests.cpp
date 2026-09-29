#include <string>
#include <string_view>
#include <vector>

#include "deco/harness/option_table.h"
#include "kota/deco/option.h"
#include "kota/zest/zest.h"

namespace kota::option {

namespace {

using test::kinds_table;
using strings = std::vector<std::string>;

/// The argv fragments `arg` renders into.
strings rendered(const ParsedArg& arg) {
    strings out;
    auto collect = [&](std::string_view fragment) {
        out.emplace_back(fragment);
    };
    kinds_table().render(arg, collect);
    return out;
}

/// The argv the one argument `line` parses into renders back into.
strings rendered(std::string_view line, ParseOptions options = {}) {
    const auto parse = test::parse_table(kinds_table(), line, options);
    ZEST_CONTEXT("rendering {}", line);
    EXPECT(parse.errors.empty());
    EXPECT(parse.args.size() == 1U);
    return parse.args.empty() ? strings{} : rendered(parse.args[0]);
}

ZEST_SUITE(deco_option_table_render) {

ZEST_CASE(options_render_as_written) {
    const std::vector<std::string_view> lines = {
        "-f",
        "-s value",
        "-jvalue",
        "-Wl,a,b",
        "--pair a b",
        "-xa b",
        "--rest a -f",
    };
    for(const auto line: lines) {
        ZEST_CONTEXT("line {}", line);
        EXPECT(rendered(line) == test::split(line));
    }
}

ZEST_CASE(joined_or_separate_renders_separate) {
    EXPECT(rendered("-ovalue") == (strings{"-o", "value"}));
}

ZEST_CASE(remaining_args_joined_render_separate) {
    EXPECT(rendered("--tailz a") == (strings{"--tail", "z", "a"}));
}

ZEST_CASE(alias_renders_as_its_target) {
    EXPECT(rendered("-h") == strings{"--help"});
}

ZEST_CASE(option_renders_with_its_first_prefix) {
    EXPECT(rendered("-Foout.o") == strings{"/Foout.o"});
}

ZEST_CASE(alias_args_render_into_the_target) {
    EXPECT(rendered("--trap-defaults") == strings{"--trap=all,undefined"});
    EXPECT(rendered("--emit-llvm") == strings{"--emit=llvm"});
}

ZEST_CASE(render_flags_choose_the_form) {
    EXPECT(rendered("-I path") == strings{"-Ipath"});
    EXPECT(rendered("-Lpath") == (strings{"-L", "path"}));
    EXPECT(rendered("-D value") == strings{"value"});
}

ZEST_CASE(input_and_unknown_render_their_spelling) {
    EXPECT(rendered("main.cc") == strings{"main.cc"});
    EXPECT(rendered("--nope a", {.greedy_unknown = true}) == (strings{"--nope", "a"}));
    EXPECT(rendered("-- a b", {.dash_dash_parsing = true, .dash_dash_packing = true}) ==
           (strings{"--", "a", "b"}));
}

ZEST_CASE(argument_of_no_option_renders_its_spelling) {
    ParsedArg arg{.id = 0, .index = 0, .next_index = 1, .spelling = "raw", .values = {}};
    arg.add_value("value");
    EXPECT(rendered(arg) == (strings{"raw", "value"}));
}

ZEST_CASE(joined_without_values_renders_its_name) {
    const ParsedArg arg{.id = test::JoinedId,
                        .index = 0,
                        .next_index = 1,
                        .spelling = "-j",
                        .values = {}};
    EXPECT(rendered(arg) == strings{"-j"});
}

};  // ZEST_SUITE(deco_option_table_render)

}  // namespace

}  // namespace kota::option
