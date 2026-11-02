// Mistakes in a struct of options, which keep it from compiling: checked here as whether the
// check of the declaration is a constant expression.

#include <cstdint>
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

/// A generated name, '_' spelled '-', that another field names as written.
struct GeneratedTwice {
    bool log_level = false;
    KANNOTATE(&other, cli::names = {"--log-level"})
    bool other = false;
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

/// A command of many options, most of them generated names with an '=' form each, in groups
/// its seven subcommands repeat: one check of them all must stay within a compiler's default
/// constant-evaluation steps, under a standard library whose strings are instrumented for ASan
/// too.
namespace large {

enum class Level : std::uint8_t { Trace, Debug, Info, Warn, Error, Off };

struct Log {
    Level log_level = Level::Warn;
};

struct Build {
    std::string configuration;
};

struct Project {
    std::optional<std::string> workspace;
    Build build;
};

struct Model {
    std::vector<std::string> scope;
    std::vector<std::string> exclude;
    int dir_depth = 0;
    std::string partition;
    std::string library;
    std::string format;
    std::vector<std::string> move;
    std::vector<std::string> merge;
    std::vector<std::string> move_entity;
    bool all_targets = false;
    std::string target;
    std::vector<std::string> define;
    Project where;
};

struct Weights {
    std::vector<std::string> annotation;
    std::string churn_since;
    int window = 0;
};

struct Edge {
    KANNOTATE(&modules, presence = required, cli::positional = true, cli::arg_num = 2)
    std::vector<std::string> modules;
    Model model;
    Log log;
};

struct Module {
    KANNOTATE(&module, cli::positional = true)
    std::string module;
    Model model;
    Log log;
};

struct File {
    KANNOTATE(&path, cli::positional = true)
    std::string path;
    Model model;
    Weights weights;
    Log log;
};

struct Obstacles {
    Model model;
    Log log;
};

struct Macros {
    Model model;
    Log log;
};

struct Impact {
    Model model;
    Weights weights;
    Log log;
};

struct Interface {
    KANNOTATE(&module, cli::positional = true)
    std::optional<std::string> module;
    Model model;
    Log log;
};

struct Modules {
    int limit = 20;
    Model model;
    Weights weights;
    Log log;
    std::optional<std::variant<Edge, Module, File, Obstacles, Macros, Impact, Interface>> operation;
};

struct Analyze {
    KANNOTATE(&operation, presence = required)
    std::variant<Modules> operation;
};

}  // namespace large

ZEST_SUITE(cli_declaration) {

ZEST_CASE(large_options_compile) {
    ZSTATIC_EXPECT(declared<large::Analyze>);
}

ZEST_CASE(sound_options_compile) {
    ZSTATIC_EXPECT(declared<Fine>);
}

ZEST_CASE(two_options_of_one_name_do_not_compile) {
    ZSTATIC_EXPECT(!declared<Twice>);
}

ZEST_CASE(generated_name_another_field_gives_does_not_compile) {
    ZSTATIC_EXPECT(!declared<GeneratedTwice>);
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
