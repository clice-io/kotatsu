#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco {

namespace {

constexpr decl::Category primary_category{.exclusive = false,
                                          .required = false,
                                          .name = "primary",
                                          .description = "primary options"};
constexpr decl::Category secondary_category{.exclusive = false,
                                            .required = false,
                                            .name = "secondary",
                                            .description = "secondary options"};
constexpr decl::Category trailing_category{.exclusive = false,
                                           .required = false,
                                           .name = "trailing",
                                           .description = "trailing options"};

enum class Color {
    Red,
    DarkGreen,
};

struct Written {
    DECO_CFG_START(required = false; category = primary_category);

    DecoFlag(names = {"-v", "--verbose"};)
    verbose;

    DecoFlagN(names = {"-n"};)
    repeat;

    DecoKV(names = {"--count"};)
    <int> count;

    DecoKVStyled(decl::KVStyle::Joined, names = {"--joined"};)
    <int> joined;

    DECO_CFG_END();

    DECO_CFG_START(required = false; category = secondary_category);

    DecoKVStyled(decl::KVStyle::JoinedOrSeparate, names = {"--split="};)
    <int> split;

    DecoKV()
    <int> auto_name;

    DecoComma(names = {"--tags"};)
    <std::vector<std::string>> tags;

    DecoMulti(2, names = {"--pair"};)
    <std::vector<std::string>> pair;

    DECO_CFG_END();

    DecoPack(required = false; category = trailing_category;)
    <std::vector<std::string>> trailing;

    DecoInput(required = false; category = primary_category;)
    <std::string> input;
};

struct Numbers {
    DECO_CFG_START(required = false);

    DecoKV()
    <double> ratio;

    DecoKV()
    <float> scale;

    DecoKV()
    <bool> enabled;

    DecoKV()
    <char> small;

    DecoKVStyled(decl::KVStyle::Joined)
    <int> x;

    DecoKVStyled(decl::KVStyle::JoinedOrSeparate)
    <std::string> label;

    DECO_CFG_END();
};

struct Colors {
    DECO_CFG_START(required = false);

    DecoKV()
    <Color> color;

    DecoComma()
    <std::vector<Color>> colors;

    DECO_CFG_END();
};

struct Aliased {
    DecoKV(required = false;)
    <Color> color;

    DecoFlagAlias(names = {"--red"}; forward = {"--color", "red"};) _;
};

struct InputList {
    DecoInput(required = false;)
    <std::vector<std::string>> inputs;
};

Written full() {
    Written written;
    written.verbose = true;
    written.repeat = std::uint32_t{2};
    written.count = 7;
    written.joined = 9;
    written.split = 3;
    written.auto_name = 11;
    written.tags = std::vector<std::string>{"a", "b"};
    written.pair = std::vector<std::string>{"left", "right"};
    written.trailing = std::vector<std::string>{"tail1", "tail2"};
    written.input = std::string("main.cc");
    return written;
}

ZEST_SUITE(deco_facade_serialize) {

ZEST_CASE(unset_options_write_nothing) {
    ZEXPECT(ser::to_argv(Written{}).empty());
}

ZEST_CASE(options_set_to_nothing_write_nothing) {
    Written written;
    written.verbose = false;
    written.repeat = std::uint32_t{0};
    written.tags = std::vector<std::string>{};
    written.pair = std::vector<std::string>{};
    ZEXPECT(ser::to_argv(written).empty());
}

ZEST_CASE(options_write_in_declaration_order_with_the_pack_last) {
    ZEXPECT(ser::to_argv(full()) == (std::vector<std::string>{"-v",
                                                              "-n",
                                                              "-n",
                                                              "--count",
                                                              "7",
                                                              "--joined9",
                                                              "--split=3",
                                                              "--auto-name",
                                                              "11",
                                                              "--tags,a,b",
                                                              "--pair",
                                                              "left",
                                                              "right",
                                                              "main.cc",
                                                              "--",
                                                              "tail1",
                                                              "tail2"}));
}

ZEST_CASE(written_argv_parses_back) {
    const auto written = full();
    auto argv = ser::to_argv(written);
    const auto parsed = cli::parse<Written>(argv);
    ZASSERT(parsed.has_value());
    const auto& options = parsed->options;
    ZEXPECT(options.verbose.as_optional() == written.verbose.as_optional());
    ZEXPECT(options.repeat.as_optional() == written.repeat.as_optional());
    ZEXPECT(options.count.as_optional() == written.count.as_optional());
    ZEXPECT(options.joined.as_optional() == written.joined.as_optional());
    ZEXPECT(options.split.as_optional() == written.split.as_optional());
    ZEXPECT(options.auto_name.as_optional() == written.auto_name.as_optional());
    ZEXPECT(options.tags.as_optional() == written.tags.as_optional());
    ZEXPECT(options.pair.as_optional() == written.pair.as_optional());
    ZEXPECT(options.trailing.as_optional() == written.trailing.as_optional());
    ZEXPECT(options.input.as_optional() == written.input.as_optional());
}

ZEST_CASE(category_selects_what_is_written) {
    ZEXPECT(ser::to_argv(full(), primary_category) ==
            (std::vector<std::string>{"-v", "-n", "-n", "--count", "7", "--joined9", "main.cc"}));

    const decl::Category* selected[] = {&secondary_category, &trailing_category};
    ZEXPECT(ser::to_argv(full(), std::span<const decl::Category* const>(selected)) ==
            (std::vector<std::string>{"--split=3",
                                      "--auto-name",
                                      "11",
                                      "--tags,a,b",
                                      "--pair",
                                      "left",
                                      "right",
                                      "--",
                                      "tail1",
                                      "tail2"}));
}

ZEST_CASE(input_list_writes_each_input) {
    InputList list;
    list.inputs = std::vector<std::string>{"a.txt", "b.txt"};
    auto argv = ser::to_argv(list);
    ZEXPECT(argv == (std::vector<std::string>{"a.txt", "b.txt"}));
    const auto parsed = cli::parse<InputList>(argv);
    ZASSERT(parsed.has_value());
    ZEXPECT(parsed->options.inputs.as_optional() == list.inputs.as_optional());
}

ZEST_CASE(floating_point_writes_what_reads_back_the_same) {
    Numbers values;
    values.ratio = 0.1234567891;
    values.scale = 1e-7F;
    auto argv = ser::to_argv(values);
    ZEXPECT(argv == (std::vector<std::string>{"--ratio", "0.1234567891", "--scale", "1e-07"}));
    const auto parsed = cli::parse<Numbers>(argv);
    ZASSERT(parsed.has_value());
    ZEXPECT(parsed->options.ratio.as_optional() == values.ratio.as_optional());
    ZEXPECT(parsed->options.scale.as_optional() == values.scale.as_optional());
}

ZEST_CASE(enum_writes_its_name) {
    Colors values;
    values.color = Color::DarkGreen;
    values.colors = std::vector<Color>{Color::Red, Color::DarkGreen};
    auto argv = ser::to_argv(values);
    ZEXPECT(argv == (std::vector<std::string>{"--color", "darkGreen", "--colors,red,darkGreen"}));
    const auto parsed = cli::parse<Colors>(argv);
    ZASSERT(parsed.has_value());
    ZEXPECT(parsed->options.color.as_optional() == values.color.as_optional());
    ZEXPECT(parsed->options.colors.as_optional() == values.colors.as_optional());
}

ZEST_CASE(bool_and_char_write_what_they_parse_from) {
    Numbers values;
    values.enabled = false;
    values.small = 'A';
    auto argv = ser::to_argv(values);
    ZEXPECT(argv == (std::vector<std::string>{"--enabled", "false", "--small", "65"}));
    const auto parsed = cli::parse<Numbers>(argv);
    ZASSERT(parsed.has_value());
    ZEXPECT(parsed->options.enabled.as_optional() == values.enabled.as_optional());
    ZEXPECT(parsed->options.small.as_optional() == values.small.as_optional());
}

ZEST_CASE(generated_name_takes_its_value_after_equals) {
    Numbers values;
    values.x = 4;
    auto argv = ser::to_argv(values);
    ZEXPECT(argv == std::vector<std::string>{"-x=4"});
    const auto parsed = cli::parse<Numbers>(argv);
    ZASSERT(parsed.has_value());
    ZEXPECT(parsed->options.x.as_optional() == std::optional(4));
}

ZEST_CASE(any_value_reads_back_after_equals) {
    // Written separate, an empty value would read as missing, and joined without the '=',
    // one starting with '=' would lose it.
    for(const auto* label: {"", "=a", "-v"}) {
        ZEST_CONTEXT("label '{}'", label);
        Numbers values;
        values.label = std::string(label);
        auto argv = ser::to_argv(values);
        ZEXPECT(argv == std::vector<std::string>{std::string("--label=") + label});
        const auto parsed = cli::parse<Numbers>(argv);
        ZASSERT(parsed.has_value());
        ZEXPECT(parsed->options.label.as_optional() == values.label.as_optional());
    }
}

ZEST_CASE(alias_writes_nothing_of_its_own) {
    Aliased values;
    values.color = Color::Red;
    ZEXPECT(ser::to_argv(values) == (std::vector<std::string>{"--color", "red"}));
}

};  // ZEST_SUITE(deco_facade_serialize)

}  // namespace

}  // namespace kota::deco
