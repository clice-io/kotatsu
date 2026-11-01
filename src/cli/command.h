#pragma once

#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "kotatsu/cli/detail/declaration.h"
#include "kotatsu/cli/parse.h"
#include "kotatsu/cli/spec.h"
#include "kotatsu/cli/table/table.h"
#include "kotatsu/meta/type_info.h"

namespace kotatsu::cli::detail {

constexpr std::uint32_t no_group = std::numeric_limits<std::uint32_t>::max();
constexpr std::uint32_t no_arg = std::numeric_limits<std::uint32_t>::max();
constexpr std::uint32_t no_exclusive = std::numeric_limits<std::uint32_t>::max();

/// How to_argv() writes a value of a named option.
enum class Form : std::uint8_t {
    /// After its name and '=': "--name=value", which reads back any value.
    Equals,
    /// Joined to its name: "-Ivalue".
    Joined,
    /// As the argument after its name.
    Separate,
};

/// A field of a command that takes arguments: an option, a positional argument, or the
/// arguments after "--".
struct Arg : FieldUse {
    const meta::field_info* field = nullptr;
    /// The innermost group it is in; no_group in the command's own struct.
    std::uint32_t group = no_group;
    /// The serialized names of the fields from the command's struct down to it.
    std::vector<std::string_view> keys = {};
    /// The declared names of the same fields, joined by '.'.
    std::string path = {};
    /// How a usage writes it: "-o|--output <FILE>".
    std::string usage = {};
    /// Its help, the field's description.
    std::string_view help = {};
    /// The name to_argv() writes it with, and how it writes a value.
    std::string write_name = {};
    Form form = Form::Separate;

    bool required() const {
        return spec.required || (spec.positional && !list && !optional);
    }
};

/// A struct inside a command: a group of its arguments.
struct Group {
    std::uint32_t parent = no_group;
    /// Whether it is a std::optional, there only when an argument of it is given.
    bool optional = false;
    std::vector<std::string_view> keys;
    /// Its heading in a usage, the field's description; none lists its arguments with the
    /// group around it.
    std::string description;
    /// For an alternative of an exclusive field, the field, by index in `exclusives`, and which
    /// alternative it is.
    std::uint32_t exclusive = no_exclusive;
    std::uint32_t alternative = 0;
};

/// A cli::exclusive field: a variant whose alternatives are groups of the command's arguments,
/// the one the argv gives an argument of first held.
struct Exclusive {
    std::vector<std::string_view> keys = {};
    /// The group it is in, whether it must be given, and the group each alternative is.
    std::uint32_t group = no_group;
    bool required = false;
    std::vector<std::uint32_t> alternatives = {};
};

struct Command;

/// The field of a command that is a variant of subcommands.
struct Subcommands {
    std::vector<std::string_view> keys = {};
    std::string path = {};
    bool optional = false;
    std::uint32_t group = no_group;
    /// The word naming each alternative, what each is (its struct's description), and
    /// whether a usage leaves it out.
    std::vector<std::string> names = {};
    std::vector<std::string_view> descriptions = {};
    std::vector<bool> hidden = {};
    std::vector<std::unique_ptr<Command>> commands = {};
};

/// What a struct takes from an argv, and the table it reads options with.
struct Command {
    const meta::struct_type_info* type = nullptr;
    std::vector<Group> groups;
    std::vector<Arg> args;
    /// The positional arguments, in declaration order, by index in `args`.
    std::vector<std::uint32_t> positionals;
    /// How many positional arguments the command takes in all.
    ArgNum positional_count = ArgNum(0);
    /// The argument that takes what follows "--".
    std::optional<std::uint32_t> rest;
    std::optional<Subcommands> subcommands;
    std::vector<Exclusive> exclusives;

    /// The option names the table views.
    std::deque<std::string> spellings;
    std::vector<Option> options;
    /// The argument each option id reads into.
    std::vector<std::uint32_t> arg_of;
    std::unique_ptr<OptTable> table;
    ParseOptions parse_options;
};

/// Builds the command `type`, a struct, describes.
std::unique_ptr<Command> build_command(const meta::type_info& type);

/// Whether the options in `root` hold the arguments of `group`: the alternative of an exclusive
/// field it is in held, and an optional group around it given.
bool present(const Command& command, const Node& root, std::uint32_t group);

/// `number` as a usage and an error write it; empty when unset.
std::string number_text(const meta::schema_number& number);

/// The node `keys` reach from `root` through objects, or null.
const Node* find_node(const Node& root, std::span<const std::string_view> keys);

/// The node `keys` reach from `root`, made of `kind` when there is none, with the objects on
/// the way.
Node& make_node(Node& root, std::span<const std::string_view> keys, Node::Kind kind);

}  // namespace kotatsu::cli::detail
