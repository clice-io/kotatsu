#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "deco/harness/argv.h"
#include "deco/harness/text.h"
#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco {

namespace {

enum class Mode {
    Alpha,
    Beta,
    Gamma,
};

enum class Spelled {
    myValue,
    Delete_,
    V123,
};

/// A value read through its own into(): the text, unless it is "bad".
struct Name {
    std::string text;

    std::optional<std::string> into(std::string_view input) {
        if(input == "bad") {
            return "bad name";
        }
        text = input;
        return std::nullopt;
    }
};

/// Like Name, but given the context, which it reports its error with.
struct LocatedName {
    std::string text;

    std::optional<std::string> into(std::string_view input, const decl::IntoContext& context) {
        if(input == "bad") {
            return context.format_error("bad located name");
        }
        text = input;
        return std::nullopt;
    }
};

/// A list read through its own into(): the values, unless one is "bad".
struct Names {
    std::vector<std::string> values;

    std::optional<std::string> into(const std::vector<std::string_view>& input) {
        for(const auto value: input) {
            if(value == "bad") {
                return "bad names";
            }
        }
        values.assign(input.begin(), input.end());
        return std::nullopt;
    }
};

/// Like Names, but given the context.
struct LocatedNames {
    std::vector<std::string> values;
    std::uint32_t begin = 0;

    std::optional<std::string> into(const std::vector<std::string_view>& input,
                                    const decl::IntoContext& context) {
        values.assign(input.begin(), input.end());
        begin = context.highlight_begin();
        return std::nullopt;
    }
};

ParsedArgOwning arg_of(std::string_view spelling,
                       std::vector<std::string> values = {},
                       std::uint32_t index = 0) {
    return ParsedArgOwning{
        .id = 3,
        .index = index,
        .spelling = std::string(spelling),
        .values = std::move(values),
    };
}

/// What reading `text` as the one value of a ScalarOption<T> gives: its error, or the value.
template <typename T>
std::optional<std::string> read(std::string_view text, std::optional<T>& out) {
    decl::ScalarOption<T> option;
    auto error = option.into(arg_of("--value", {std::string(text)}));
    if(option.has_value()) {
        out = *option;
    }
    return error;
}

ZEST_SUITE(deco_facade_decl_into) {

ZEST_CASE(flag_is_set_by_its_argument) {
    decl::FlagOption<bool> flag;
    EXPECT(!flag.into(arg_of("-v")));
    ASSERT(flag.has_value());
    EXPECT(*flag);
}

ZEST_CASE(flag_given_a_value_fails) {
    decl::FlagOption<bool> flag;
    const auto error = flag.into(arg_of("-v", {"1"}));
    ASSERT(error.has_value());
    EXPECT(*error == "flag option does not accept values");
    EXPECT(!flag.has_value());
}

ZEST_CASE(counting_flag_counts_each_argument) {
    decl::FlagOption<std::uint32_t> count;
    for(int i = 0; i < 3; ++i) {
        ZEST_CONTEXT("argument {}", i);
        EXPECT(!count.into(arg_of("-v")));
    }
    ASSERT(count.has_value());
    EXPECT(*count == 3U);
}

ZEST_CASE(scalar_takes_one_value) {
    decl::ScalarOption<std::string> option;
    EXPECT(!option.into(arg_of("-o", {"out"})));
    ASSERT(option.has_value());
    EXPECT(*option == "out");
}

ZEST_CASE(scalar_given_two_values_fails) {
    decl::ScalarOption<std::string> option;
    const auto error = option.into(arg_of("-o", {"a", "b"}));
    ASSERT(error.has_value());
    EXPECT(*error == "expected exactly one value");
    EXPECT(!option.has_value());
}

ZEST_CASE(bool_reads_its_spellings) {
    const std::vector<std::pair<std::string_view, bool>> spellings = {
        {"1",     true },
        {"true",  true },
        {"TRUE",  true },
        {"yes",   true },
        {"On",    true },
        {"0",     false},
        {"false", false},
        {"No",    false},
        {"OFF",   false},
    };
    for(const auto [text, expected]: spellings) {
        ZEST_CONTEXT("text {}", text);
        std::optional<bool> value;
        EXPECT(!read(text, value));
        EXPECT(value == std::optional(expected));
    }
}

ZEST_CASE(bool_of_other_text_fails) {
    std::optional<bool> value;
    const auto error = read("maybe", value);
    ASSERT(error.has_value());
    EXPECT(*error == "invalid boolean value: maybe");
    EXPECT(!value.has_value());
}

ZEST_CASE(integer_reads_decimal) {
    std::optional<int> value;
    EXPECT(!read("-42", value));
    EXPECT(value == std::optional(-42));
}

ZEST_CASE(integer_of_other_text_fails) {
    for(const auto text: {"4x", "x4", " 4", "", "99999999999"}) {
        ZEST_CONTEXT("text '{}'", text);
        std::optional<int> value;
        const auto error = read(text, value);
        ASSERT(error.has_value());
        EXPECT(*error == std::string("invalid integer value: ") + text);
    }
}

ZEST_CASE(unsigned_of_a_negative_fails) {
    std::optional<unsigned> value;
    const auto error = read("-1", value);
    ASSERT(error.has_value());
    EXPECT(*error == "invalid integer value: -1");
    EXPECT(!value.has_value());
}

ZEST_CASE(floating_point_reads_decimal_and_exponent) {
    std::optional<double> value;
    EXPECT(!read("3.25", value));
    EXPECT(value == std::optional(3.25));
    EXPECT(!read("-1e3", value));
    EXPECT(value == std::optional(-1000.0));

    std::optional<float> single;
    EXPECT(!read("0.5", single));
    EXPECT(single == std::optional(0.5F));
}

ZEST_CASE(floating_point_of_other_text_fails) {
    // strtod would read an empty text as 0 and skip leading spaces.
    for(const auto text: {"3.2x", "x", "", " 1.5"}) {
        ZEST_CONTEXT("text '{}'", text);
        std::optional<double> value;
        const auto error = read(text, value);
        ASSERT(error.has_value());
        EXPECT(*error == std::string("invalid floating-point value: ") + text);
        EXPECT(!value.has_value());
    }
}

ZEST_CASE(floating_point_out_of_range_fails) {
    std::optional<double> value;
    const auto error = read("1e999", value);
    ASSERT(error.has_value());
    EXPECT(*error == "floating-point value out of range: 1e999");
}

ZEST_CASE(enum_reads_its_name_in_either_camel_case) {
    for(const auto text: {"beta", "Beta"}) {
        ZEST_CONTEXT("text {}", text);
        std::optional<Mode> value;
        EXPECT(!read(text, value));
        EXPECT(value == std::optional(Mode::Beta));
    }
}

ZEST_CASE(enum_reads_snake_keyword_and_numeric_spellings) {
    std::optional<Spelled> value;
    EXPECT(!read("my_value", value));
    EXPECT(value == std::optional(Spelled::myValue));
    EXPECT(!read("delete", value));
    EXPECT(value == std::optional(Spelled::Delete_));
    EXPECT(!read("123", value));
    EXPECT(value == std::optional(Spelled::V123));
}

ZEST_CASE(enum_of_other_text_fails) {
    std::optional<Mode> value;
    const auto error = read("delta", value);
    ASSERT(error.has_value());
    EXPECT(*error == "invalid enum value: delta (supported: alpha, beta, gamma)");
}

ZEST_CASE(custom_scalar_reads_through_into) {
    decl::ScalarOption<Name> option;
    EXPECT(!option.into(arg_of("--name", {"alice"})));
    ASSERT(option.has_value());
    EXPECT(option->text == "alice");
}

ZEST_CASE(custom_scalar_bad_value_fails) {
    // Located at the value, like the error of a value deco reads itself.
    const auto renderer = test::tagged_renderer();
    const auto argv = test::args("-v", "--name", "bad");
    decl::ScalarOption<Name> option;
    const auto error =
        option.into(arg_of("--name", {"bad"}, 1), decl::IntoContext(argv, 1, 3, &renderer));
    ASSERT(error.has_value());
    EXPECT(*error == "ERR<2:bad name>");
}

ZEST_CASE(custom_scalar_with_context_bad_value_fails) {
    // Located by the type itself, with the context it is given: once.
    const auto renderer = test::tagged_renderer();
    const auto argv = test::args("--name", "bad");
    decl::ScalarOption<LocatedName> option;
    const auto error =
        option.into(arg_of("--name", {"bad"}), decl::IntoContext(argv, 0, 2, &renderer));
    ASSERT(error.has_value());
    EXPECT(*error == "ERR<1:bad located name>");
}

ZEST_CASE(scalar_bad_value_fails) {
    // Located at the element that holds the value, not at the option's name.
    const auto renderer = test::tagged_renderer();
    const auto argv = test::args("--count", "x");
    decl::ScalarOption<int> option;
    const auto error =
        option.into(arg_of("--count", {"x"}), decl::IntoContext(argv, 0, 2, &renderer));
    ASSERT(error.has_value());
    EXPECT(*error == "ERR<1:invalid integer value: x>");
}

ZEST_CASE(input_reads_its_spelling) {
    decl::InputOption<int> input;
    EXPECT(!input.into(arg_of("123")));
    EXPECT(input.as_optional() == std::optional(123));
}

ZEST_CASE(input_reads_one_value) {
    decl::InputOption<std::string> input;
    EXPECT(!input.into(arg_of("--", {"tail"})));
    EXPECT(input.as_optional() == std::optional<std::string>("tail"));
}

ZEST_CASE(input_given_two_values_fails) {
    decl::InputOption<std::string> input;
    const auto error = input.into(arg_of("--", {"a", "b"}));
    ASSERT(error.has_value());
    EXPECT(*error == "input option expects at most one value");
}

ZEST_CASE(input_list_gathers_every_argument) {
    decl::InputOption<std::vector<int>> inputs;
    EXPECT(!inputs.into(arg_of("7")));
    EXPECT(!inputs.into(arg_of("--", {"8", "9"})));
    EXPECT(inputs.as_optional() == std::optional(std::vector<int>{7, 8, 9}));
}

ZEST_CASE(input_list_bad_argument_fails) {
    // The list keeps what it had, and goes on from there.
    decl::InputOption<std::vector<int>> inputs;
    EXPECT(!inputs.into(arg_of("7")));
    const auto error = inputs.into(arg_of("x"));
    ASSERT(error.has_value());
    EXPECT(*error == "invalid vector value at index 1: invalid integer value: x");
    EXPECT(!inputs.into(arg_of("8")));
    EXPECT(inputs.as_optional() == std::optional(std::vector<int>{7, 8}));
}

ZEST_CASE(vector_reads_each_value) {
    decl::VectorOption<std::vector<Mode>> option;
    EXPECT(!option.into(arg_of("--modes", {"alpha", "gamma"})));
    EXPECT(option.as_optional() == std::optional(std::vector<Mode>{Mode::Alpha, Mode::Gamma}));
}

ZEST_CASE(vector_reads_no_values_as_empty) {
    decl::VectorOption<std::vector<int>> option;
    EXPECT(!option.into(arg_of("--list")));
    ASSERT(option.has_value());
    EXPECT(option->empty());
}

ZEST_CASE(vector_bad_value_fails) {
    decl::VectorOption<std::vector<Mode>> option;
    const auto error = option.into(arg_of("--modes", {"alpha", "delta"}));
    ASSERT(error.has_value());
    EXPECT(*error ==
           "invalid vector value at index 1: invalid enum value: delta "
           "(supported: alpha, beta, gamma)");
    EXPECT(!option.has_value());
}

ZEST_CASE(custom_vector_reads_through_into) {
    decl::VectorOption<Names> option;
    EXPECT(!option.into(arg_of("--names", {"x", "y"})));
    ASSERT(option.has_value());
    EXPECT(option->values == (std::vector<std::string>{"x", "y"}));
}

ZEST_CASE(custom_vector_bad_value_fails) {
    const auto renderer = test::tagged_renderer();
    const auto argv = test::args("--names", "bad");
    decl::VectorOption<Names> option;
    const auto error =
        option.into(arg_of("--names", {"bad"}), decl::IntoContext(argv, 0, 2, &renderer));
    ASSERT(error.has_value());
    EXPECT(*error == "ERR<0:bad names>");
}

ZEST_CASE(custom_vector_with_context_is_given_it) {
    const auto argv = test::args("-v", "--names", "x");
    decl::VectorOption<LocatedNames> option;
    EXPECT(!option.into(arg_of("--names", {"x"}, 1), decl::IntoContext(argv, 1, 3)));
    ASSERT(option.has_value());
    EXPECT(option->values == std::vector<std::string>{"x"});
    EXPECT(option->begin == 1U);
}

};  // ZEST_SUITE(deco_facade_decl_into)

}  // namespace

}  // namespace kota::deco
