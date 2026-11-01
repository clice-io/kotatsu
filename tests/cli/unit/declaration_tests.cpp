// Mistakes in a struct of options, which keep it from compiling: checked here as whether the
// check of the declaration is a constant expression.

#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

#include "kotatsu/cli/cli.h"
#include "kotatsu/zest/zest.h"

namespace kotatsu::cli {

namespace {

/// Whether options `T` declare what cli takes, so that parse<T>() compiles.
template <typename T>
concept declared = requires {
    typename std::bool_constant<detail::check_declaration(codec::type_info::of<T, cli::format>())>;
};

struct Serve {
    int port = 0;
};

struct Fine {
    bool verbose = false;
    KANNOTATE(&separate, cli::names = {"--x"})
    std::string separate;
    KANNOTATE(&joined, cli::names = {"--x="})
    std::string joined;
    KANNOTATE(&paths, cli::positional = true)
    std::vector<std::string> paths;
    std::optional<std::variant<Serve>> command;
};

struct Twice {
    KANNOTATE(&first, cli::names = {"-o"})
    std::string first;
    KANNOTATE(&second, cli::names = {"-o"})
    std::string second;
};

struct Inner {
    bool verbose = false;
};

/// A group naming an option as the struct around it does.
struct TwiceInAGroup {
    bool verbose = false;
    Inner inner;
};

/// A Joined option of a generated name, whose only form another field names.
struct EqualsTaken {
    KANNOTATE(&level, cli::style = KVStyle::Joined)
    std::string level;
    KANNOTATE(&other, cli::names = {"--level="})
    std::string other;
};

struct Unprefixed {
    KANNOTATE(&out, cli::names = {"out"})
    std::string out;
};

struct CommaScalar {
    KANNOTATE(&t, cli::comma = true)
    std::string t;
};

struct Loop {
    KANNOTATE(&a, cli::names = {"-a"}, cli::forward = {"-b"})
    cli::Alias a;
    KANNOTATE(&b, cli::names = {"-b"}, cli::forward = {"-a"})
    cli::Alias b;
};

struct TwoRests {
    KANNOTATE(&first, cli::rest = true)
    std::vector<std::string> first;
    KANNOTATE(&second, cli::rest = true)
    std::vector<std::string> second;
};

struct ScalarRest {
    KANNOTATE(&rest, cli::rest = true)
    std::string rest;
};

struct TwoSubcommandFields {
    std::variant<Serve> first;
    std::variant<Serve> second;
};

struct ScalarSubcommand {
    std::variant<Serve, int> command;
};

namespace one {

struct Serve {};

}  // namespace one

struct SameWords {
    std::variant<Serve, one::Serve> command;
};

struct HiddenUnknown {
    KANNOTATE(&command, cli::hidden_sub = {"worker"})
    std::variant<Serve> command;
};

struct InnerTwice {
    std::variant<Twice> command;
};

struct Choice {
    int a = 0;
};

struct Nested {
    KANNOTATE(&inner, cli::exclusive = true)
    std::variant<Choice, Serve> inner;
};

/// An exclusive field inside an alternative of another.
struct NestedExclusive {
    KANNOTATE(&outer, cli::exclusive = true)
    std::variant<Nested, Choice> outer;
};

struct ExclusiveScalar {
    KANNOTATE(&value, cli::exclusive = true)
    int value;
};

/// Two alternatives naming an option the same.
struct ExclusiveTwice {
    KANNOTATE(&one, cli::exclusive = true)
    std::variant<Choice, Inner> one;
    KANNOTATE(&two, cli::exclusive = true)
    std::variant<Inner, Serve> two;
};

struct AlsoVerbose {
    bool verbose = false;
};

/// One exclusive field whose two alternatives name an option the same.
struct OneFieldTwice {
    KANNOTATE(&field, cli::exclusive = true)
    std::variant<Inner, AlsoVerbose> field;
};

struct ExclusiveOfAScalar {
    KANNOTATE(&field, cli::exclusive = true)
    std::variant<Choice, int> field;
};

struct ExclusiveStruct {
    KANNOTATE(&field, cli::exclusive = true)
    Choice field;
};

struct Top {
    using signature = int();
};

struct VariantAndCommands {
    std::variant<Serve> first;
    cli::Commands<Top> second;
};

struct WithCommands {
    cli::Commands<Top> command;
};

struct CommandsInAnAlternative {
    KANNOTATE(&field, cli::exclusive = true)
    std::variant<WithCommands, Choice> field;
};

struct First {
    KANNOTATE(&a, cli::positional = true)
    std::string a;
};

struct Second {
    KANNOTATE(&b, cli::positional = true)
    std::string b;
};

struct PositionalsInTwoAlternatives {
    KANNOTATE(&field, cli::exclusive = true)
    std::variant<First, Second> field;
};

ZEST_SUITE(cli_declaration) {

ZEST_CASE(sound_options_compile) {
    ZSTATIC_EXPECT(declared<Fine>);
}

ZEST_CASE(two_options_of_one_name_do_not_compile) {
    ZSTATIC_EXPECT(!declared<Twice>);
}

ZEST_CASE(group_naming_an_option_of_its_command_does_not_compile) {
    ZSTATIC_EXPECT(!declared<TwiceInAGroup>);
}

ZEST_CASE(only_form_another_field_names_does_not_compile) {
    ZSTATIC_EXPECT(!declared<EqualsTaken>);
}

ZEST_CASE(name_without_a_prefix_does_not_compile) {
    ZSTATIC_EXPECT(!declared<Unprefixed>);
}

ZEST_CASE(comma_scalar_does_not_compile) {
    ZSTATIC_EXPECT(!declared<CommaScalar>);
}

ZEST_CASE(alias_of_an_alias_does_not_compile) {
    ZSTATIC_EXPECT(!declared<Loop>);
}

ZEST_CASE(two_rests_do_not_compile) {
    ZSTATIC_EXPECT(!declared<TwoRests>);
}

ZEST_CASE(scalar_rest_does_not_compile) {
    ZSTATIC_EXPECT(!declared<ScalarRest>);
}

ZEST_CASE(two_fields_of_subcommands_do_not_compile) {
    ZSTATIC_EXPECT(!declared<TwoSubcommandFields>);
}

ZEST_CASE(subcommand_of_no_struct_does_not_compile) {
    ZSTATIC_EXPECT(!declared<ScalarSubcommand>);
}

ZEST_CASE(two_subcommands_of_one_word_do_not_compile) {
    ZSTATIC_EXPECT(!declared<SameWords>);
}

ZEST_CASE(hidden_subcommand_of_another_word_does_not_compile) {
    ZSTATIC_EXPECT(!declared<HiddenUnknown>);
}

ZEST_CASE(mistake_in_a_subcommand_does_not_compile) {
    ZSTATIC_EXPECT(!declared<InnerTwice>);
}

ZEST_CASE(options_of_no_struct_do_not_compile) {
    ZSTATIC_EXPECT(!declared<std::tuple<int>>);
}

ZEST_CASE(nested_exclusive_fields_do_not_compile) {
    ZSTATIC_EXPECT(declared<Nested>);
    ZSTATIC_EXPECT(!declared<NestedExclusive>);
}

ZEST_CASE(exclusive_scalar_does_not_compile) {
    ZSTATIC_EXPECT(!declared<ExclusiveScalar>);
}

ZEST_CASE(alternatives_naming_an_option_the_same_do_not_compile) {
    ZSTATIC_EXPECT(!declared<ExclusiveTwice>);
}

ZEST_CASE(alternatives_of_one_field_naming_an_option_the_same_do_not_compile) {
    ZSTATIC_EXPECT(!declared<OneFieldTwice>);
}

ZEST_CASE(exclusive_alternative_of_no_struct_does_not_compile) {
    ZSTATIC_EXPECT(!declared<ExclusiveOfAScalar>);
}

ZEST_CASE(exclusive_struct_does_not_compile) {
    ZSTATIC_EXPECT(!declared<ExclusiveStruct>);
}

ZEST_CASE(variant_and_commands_fields_do_not_compile) {
    ZSTATIC_EXPECT(declared<WithCommands>);
    ZSTATIC_EXPECT(!declared<VariantAndCommands>);
}

ZEST_CASE(commands_in_an_alternative_do_not_compile) {
    ZSTATIC_EXPECT(!declared<CommandsInAnAlternative>);
}

ZEST_CASE(positionals_in_two_alternatives_do_not_compile) {
    ZSTATIC_EXPECT(!declared<PositionalsInTwoAlternatives>);
}

};  // ZEST_SUITE(cli_declaration)

}  // namespace

}  // namespace kotatsu::cli
