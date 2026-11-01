#pragma once

#include <cstdint>
#include <limits>
#include <string_view>

#include "kotatsu/meta/annotation.h"
#include "kotatsu/meta/spec.h"

namespace kotatsu::cli {

/// How a named option with a value takes it: joined to its name (`-O2`, `--level=2`), as the
/// next argument (`-O 2`), or either, the default. Under JoinedOrSeparate each name picks its
/// own form: one ending in '=' or ':' takes its value joined, any other takes it as the next
/// argument or after '='.
enum class KVStyle : std::uint8_t {
    Joined = 1 << 0,
    Separate = 1 << 1,
    JoinedOrSeparate = Joined | Separate,
};

/// How many arguments a positional list takes when it is given: `min` to `max`. A number is
/// exactly that many; the default is one or more. Whether it may be left out altogether is
/// `required`'s to say.
struct ArgNum {
    constexpr static std::uint32_t unbounded = std::numeric_limits<std::uint32_t>::max();

    std::uint32_t min = 1;
    std::uint32_t max = unbounded;

    constexpr ArgNum() = default;

    constexpr ArgNum(std::uint32_t count) : min{count}, max{count} {}

    constexpr ArgNum(std::uint32_t min, std::uint32_t max) : min{min}, max{max} {}

    constexpr bool operator==(const ArgNum&) const = default;
};

namespace detail {

struct CommandRegistry;

/// The set of subcommands a cli::Commands field reads. `given` says there is one: a constant
/// expression may not compare `registry` with null on every compiler.
struct CommandSet {
    bool given = false;
    const CommandRegistry& (*registry)() = nullptr;
};

}  // namespace detail

/// What a field's `cli::` annotation entries say about it on a command line, beside the
/// `description` its help is. A field without them is an option all the same, named after it.
struct ArgSpec {
    /// The names it is given by, as written: "-o", "--output", "/out". Without, its declared
    /// name with '_' spelled '-', after "-" when one letter long and "--" otherwise.
    meta::name_list names = {};
    /// What a usage shows for its value: "<FILE>". Without, the names of an enum, else
    /// "<value>".
    std::string_view meta_var = {};
    /// Whether it takes positional arguments rather than being named. Positional fields take
    /// the arguments in the order they are declared; a list takes as many as follow.
    bool positional = false;
    /// Whether it takes the arguments after "--".
    bool rest = false;
    /// Whether a flag counts how often it is given (`-v -v -v`) rather than whether it is.
    bool count = false;
    /// Whether it must be given. A positional scalar must be, unless it is a std::optional.
    bool required = false;
    /// Whether giving it ends the parse: nothing after it is read, and nothing required is
    /// checked. The help option's does.
    bool stop = false;
    KVStyle style = KVStyle::JoinedOrSeparate;
    /// Whether a list takes its values joined to its name by commas: "--tags,a,b".
    bool comma = false;
    /// How many arguments a positional list takes, when given.
    ArgNum arg_num = {};
    /// For an alias, a field of type cli::Alias: the arguments it stands for.
    meta::name_list forward = {};
    /// Whether a usage leaves it out: an option or argument for other programs, not people.
    bool hidden = false;
    /// For a std::variant of structs, whether its alternatives are groups of the command's own
    /// arguments of which the argv gives one, rather than subcommands: the argv picks the one
    /// whose argument it gives first, and an argument of another then conflicts.
    bool exclusive = false;
    /// For a cli::Commands field, which sets it, the subcommands files added to its set.
    detail::CommandSet commands = {};
    /// For a field of subcommands, the words of those a usage leaves out.
    meta::name_list hidden_sub = {};

    constexpr void validate() const {
        if(positional && rest) {
            KOTATSU_THROW("cli: an argument is positional or takes the rest, not both");
        }
        if(arg_num.min == 0 || arg_num.min > arg_num.max) {
            KOTATSU_THROW("cli: arg_num takes 1 or more, its min no more than its max");
        }
    }
};

/// The entries of ArgSpec in a KANNOTATE: `cli::names = {"-o", "--output"}`.
[[maybe_unused]] constexpr inline meta::dsl::member_proxy<&ArgSpec::names> names{};
[[maybe_unused]] constexpr inline meta::dsl::member_proxy<&ArgSpec::meta_var> meta_var{};
[[maybe_unused]] constexpr inline meta::dsl::member_proxy<&ArgSpec::positional> positional{};
[[maybe_unused]] constexpr inline meta::dsl::member_proxy<&ArgSpec::rest> rest{};
[[maybe_unused]] constexpr inline meta::dsl::member_proxy<&ArgSpec::count> count{};
[[maybe_unused]] constexpr inline meta::dsl::member_proxy<&ArgSpec::required> required{};
[[maybe_unused]] constexpr inline meta::dsl::member_proxy<&ArgSpec::stop> stop{};
[[maybe_unused]] constexpr inline meta::dsl::member_proxy<&ArgSpec::style> style{};
[[maybe_unused]] constexpr inline meta::dsl::member_proxy<&ArgSpec::comma> comma{};
[[maybe_unused]] constexpr inline meta::dsl::member_proxy<&ArgSpec::arg_num> arg_num{};
[[maybe_unused]] constexpr inline meta::dsl::member_proxy<&ArgSpec::forward> forward{};
[[maybe_unused]] constexpr inline meta::dsl::member_proxy<&ArgSpec::hidden> hidden{};
[[maybe_unused]] constexpr inline meta::dsl::member_proxy<&ArgSpec::exclusive> exclusive{};
[[maybe_unused]] constexpr inline meta::dsl::member_proxy<&ArgSpec::hidden_sub> hidden_sub{};

/// A field that stands for other arguments: its names, given, are read as `cli::forward`.
/// It holds nothing.
struct Alias {
    constexpr bool operator==(const Alias&) const = default;
};

/// The standard help option, `-h` and `--help`, as a bundle of entries: given, it ends the
/// parse where it stands, so that nothing required is missed, and the caller finds it set and
/// prints the usage.
///
///     KANNOTATE(help, cli::help)
///     bool help = false;
[[maybe_unused]] constexpr inline auto help =
    meta::make_annotation(meta::dsl::description = "display this help and exit",
                          names = {"-h", "--help"},
                          stop = true);

}  // namespace kotatsu::cli
