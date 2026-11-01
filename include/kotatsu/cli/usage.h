#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include "kotatsu/cli/error.h"
#include "kotatsu/cli/parse.h"
#include "kotatsu/cli/to_argv.h"

namespace kotatsu::cli {

/// An option, positional argument or subcommand as a usage lists it.
struct UsageEntry {
    enum class Kind : std::uint8_t {
        Option,
        /// A positional argument, or the arguments after "--".
        Positional,
        Subcommand,
    };

    Kind kind = Kind::Option;
    /// How it is written: "-o|--output <FILE>", "<PATH>...", "-- <ARG>...", "serve".
    std::string usage = {};
    /// Its help: the field's description, or the subcommand struct's.
    std::string help = {};
    /// The heading it is listed under, the description of the group it is in; empty for the
    /// command's own.
    std::string group = {};
    /// Whether every occurrence of it counts, which its help then says.
    bool repeatable = false;
    /// The value it has when the argv leaves it out, as an argv writes it: "0", "a, b"; empty
    /// for none, a flag, an empty list or an empty optional.
    std::string default_value = {};
    /// The numbers it takes, from its field's minimum and maximum: "1..64", "1..", "..64";
    /// empty when unbounded.
    std::string range = {};
};

namespace detail {

/// The options `T` declares as a default-constructed `T` holds them, written as to_argv() writes
/// them, their texts kept in `texts`.
template <typename T>
Node defaults_of(std::deque<std::string>& texts) {
    Node root;
    Writer writer{root, texts};
    codec::rich_error error;
    codec::scoped_context<codec::rich_error> guard(error);
    if(!codec::encode_value<decode_config>(writer, T{})) {
        return Node{};
    }
    return root;
}

std::vector<UsageEntry> usage_entries(const Command& command, const Node& defaults);

std::string usage(const Command& command,
                  const Node& defaults,
                  std::string_view usage_line,
                  const Style& style);

}  // namespace detail

/// The entries a usage of the options `T` declares lists, in declaration order: what a program
/// lays out its own help from.
template <typename T>
std::vector<UsageEntry> usage_entries() {
    std::deque<std::string> texts;
    return detail::usage_entries(detail::command_of<T>(), detail::defaults_of<T>(texts));
}

/// The usage of a program taking the options `T` declares: what `T` is (its static
/// `description`), `usage: <usage_line>`, its options, by group, and its subcommands.
template <typename T>
std::string usage(std::string_view usage_line, const Style& style = {}) {
    std::deque<std::string> texts;
    return detail::usage(detail::command_of<T>(), detail::defaults_of<T>(texts), usage_line, style);
}

}  // namespace kotatsu::cli
