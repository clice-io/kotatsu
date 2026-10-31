#pragma once

#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "kotatsu/cli/spec.h"
#include "kotatsu/cli/table/table.h"
#include "kotatsu/meta/type_info.h"

namespace kotatsu::cli::detail {

/// How an argument takes its values.
enum class Shape : std::uint8_t {
    /// A bool: given or not.
    Flag,
    /// An integer counting how often it is given.
    Count,
    /// One value an occurrence.
    Value,
    /// A tuple's elements an occurrence, one value each.
    Tuple,
    /// A cli::Alias: what it stands for, read in its place.
    Alias,
};

constexpr std::uint32_t no_group = std::numeric_limits<std::uint32_t>::max();

/// How to_argv() writes a value of a named option.
enum class Form : std::uint8_t {
    /// After its name and '=': "--name=value", which reads back any value.
    Equals,
    /// Joined to its name: "-Ivalue".
    Joined,
    /// As the argument after its name.
    Separate,
};

/// A field of a command that takes arguments: an option, or a positional argument.
struct Arg {
    const meta::field_info* field;
    ArgSpec spec;
    Shape shape;
    /// Whether it collects every occurrence, a list.
    bool list = false;
    /// Whether it is a std::optional, which a positional one may be left out for.
    bool optional_type = false;
    bool positional = false;
    std::uint32_t tuple_size = 0;
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
        return spec.required || (positional && !list && !optional_type);
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
    std::string_view description;
};

struct Command;

/// The field of a command that is a variant of subcommands.
struct Subcommands {
    std::vector<std::string_view> keys = {};
    std::string path = {};
    bool optional = false;
    std::uint32_t group = no_group;
    /// The word naming each alternative, and what each is, its struct's description.
    std::vector<std::string> names = {};
    std::vector<std::string_view> descriptions = {};
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

}  // namespace kotatsu::cli::detail
