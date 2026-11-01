#include "kotatsu/cli/parse.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <compare>
#include <cstdlib>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "command.h"
#include "error_at.h"
#include "kotatsu/cli/table/table.h"

namespace kotatsu::cli::detail {

namespace {

bool iequals_ascii(std::string_view lhs, std::string_view rhs) {
    auto lower = [](char ch) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    };
    return lhs.size() == rhs.size() && std::ranges::equal(lhs, rhs, {}, lower, lower);
}

/// How many positional arguments `num` is, in words: "1 input argument", "at least 2 input
/// arguments", "1 to 3 input arguments".
std::string input_args_text(ArgNum num) {
    const std::string_view noun = num.min == 1 ? "input argument" : "input arguments";
    if(num.min == num.max) {
        return std::format("{} {}", num.min, noun);
    }
    if(num.max == ArgNum::unbounded) {
        return std::format("at least {} {}", num.min, noun);
    }
    return std::format("{} to {} input arguments", num.min, num.max);
}

/// `names` joined by ", ".
std::string join_names(std::span<const std::string_view> names) {
    std::string joined;
    for(auto name: names) {
        joined += joined.empty() ? "" : ", ";
        joined += name;
    }
    return joined;
}

/// `text` as the number it reads as, exactly; monostate when it reads as none.
meta::schema_number number_of(std::string_view text) {
    if(std::int64_t value = 0; !read_number(text, value)) {
        return value;
    }
    if(std::uint64_t value = 0; !read_number(text, value)) {
        return value;
    }
    if(double value = 0; !read_number(text, value)) {
        return value;
    }
    return std::monostate{};
}

/// Edits from `a` to `b`: insertions, deletions, substitutions and swaps of two neighbours.
std::size_t edit_distance(std::string_view a, std::string_view b) {
    std::vector<std::vector<std::size_t>> d(a.size() + 1, std::vector<std::size_t>(b.size() + 1));
    for(std::size_t i = 0; i <= a.size(); ++i) {
        d[i][0] = i;
    }
    for(std::size_t j = 0; j <= b.size(); ++j) {
        d[0][j] = j;
    }
    for(std::size_t i = 1; i <= a.size(); ++i) {
        for(std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost});
            if(i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) {
                d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1);
            }
        }
    }
    return d[a.size()][b.size()];
}

/// The name of `names` closest to `word`, without the '=' or ',' a name takes its value after,
/// when it is a third of `word` or fewer edits away; empty when none is.
template <typename Names>
std::string closest(std::string_view word, const Names& names) {
    std::string best;
    std::size_t best_distance = std::max<std::size_t>(1, word.size() / 3) + 1;
    for(const auto& spelling: names) {
        std::string_view name = spelling;
        if(name.ends_with('=') || name.ends_with(',')) {
            name.remove_suffix(1);
        }
        const auto distance = edit_distance(word, name);
        if(distance < best_distance) {
            best_distance = distance;
            best = name;
        }
    }
    return best;
}

/// The alternatives of `exclusive`, each by the usage of its first argument: "<LOCATOR> or
/// --spec <value>".
std::string alternatives_text(const Command& command, const Exclusive& exclusive) {
    std::string text;
    for(const auto alternative: exclusive.alternatives) {
        for(const auto& arg: command.args) {
            auto group = arg.group;
            while(group != no_group && group != alternative) {
                group = command.groups[group].parent;
            }
            if(group == alternative) {
                text += text.empty() ? "" : " or ";
                text += arg.usage;
                break;
            }
        }
    }
    return text;
}

/// The words of the subcommands a usage lists, which a suggestion may name.
std::vector<std::string_view> shown_names(const Subcommands& subcommands) {
    std::vector<std::string_view> names;
    for(std::size_t i = 0; i < subcommands.names.size(); ++i) {
        if(!subcommands.hidden[i]) {
            names.push_back(subcommands.names[i]);
        }
    }
    return names;
}

/// An error at the elements of `argv` that [begin, end) of `reading.active` come from: the
/// first's, through the last's, or the end of `argv` past the active ones; in the subcommands
/// named before them.
Error error_from(const Reading& reading,
                 string_list_ref argv,
                 Error::Kind kind,
                 std::string message,
                 std::uint32_t begin,
                 std::uint32_t end) {
    const auto size = static_cast<std::uint32_t>(reading.active.size());
    const auto argc = static_cast<std::uint32_t>(argv.size());
    const auto first = begin >= size ? argc : reading.origin[begin];
    const auto past = end <= begin ? first : end > size ? argc : reading.origin[end - 1] + 1;
    auto error = error_at(kind, std::move(message), argv, first, std::max(first, past));
    for(const auto word: reading.subcommand_words) {
        if(word < begin) {
            error.subcommands.emplace_back(reading.active[word]);
        }
    }
    return error;
}

/// Reads one argv against a command, and the subcommands it names.
class ArgvReader {
    Reading& reading;
    string_list_ref argv;
    /// Whether an argument that ends the parse was read.
    bool stopped = false;

public:
    ArgvReader(Reading& reading, string_list_ref argv) : reading(reading), argv(argv) {
        reading.active.assign(argv.begin(), argv.end());
        reading.origin.resize(argv.size());
        for(std::uint32_t i = 0; i < reading.origin.size(); ++i) {
            reading.origin[i] = i;
        }
    }

    Error
        error(Error::Kind kind, std::string message, std::uint32_t begin, std::uint32_t end) const {
        return error_from(reading, argv, kind, std::move(message), begin, end);
    }

    /// The state of a command being read.
    struct Level {
        const Command* command;
        Node* root;
        /// What the paths of its arguments' Given go after: "" for the options struct's own,
        /// "command." for a subcommand's.
        std::string prefix;
        /// The occurrences of each argument of the command so far.
        std::vector<std::uint32_t> occurrences;
        /// For each exclusive field, the argument that chose the alternative it holds.
        std::vector<std::uint32_t> chosen_by;
        /// The positional argument the next input goes to.
        std::size_t positional = 0;
        /// Where the last argument read ended, in `reading.active`.
        std::uint32_t cursor = 0;
    };

    /// The node argument `arg` of `level` stores into: its field's under the objects of the
    /// groups around it, made as needed.
    static Node& node_of(Level& level, const Arg& arg) {
        const auto kind = arg.shape == Shape::Flag                ? Node::Kind::Flag
                          : arg.shape == Shape::Count             ? Node::Kind::Count
                          : arg.list || arg.shape == Shape::Tuple ? Node::Kind::List
                                                                  : Node::Kind::Text;
        return make_node(*level.root, arg.keys, kind);
    }

    /// Stores `values`, read from [begin, end) of `reading.active`, into argument `arg_index`
    /// of `level`, or says why its field's constraints refuse one.
    std::optional<Error> store(Level& level,
                               std::uint32_t arg_index,
                               std::span<const std::string_view> values,
                               std::uint32_t begin,
                               std::uint32_t end) {
        const auto& arg = level.command->args[arg_index];
        if(auto err = choose(level, arg_index, begin, end)) {
            return err;
        }
        ++level.occurrences[arg_index];
        Node& node = node_of(level, arg);
        // A value marks its own element when it is one, else the whole argument: "-Ia" or
        // "--tags,a,b".
        auto from = begin;
        auto text = [&](std::string_view value) {
            for(auto i = from; i < end; ++i) {
                if(reading.active[i].data() == value.data() &&
                   reading.active[i].size() == value.size()) {
                    from = i + 1;
                    return Node{.kind = Node::Kind::Text, .text = value, .begin = i, .end = i + 1};
                }
            }
            return Node{.kind = Node::Kind::Text, .text = value, .begin = begin, .end = end};
        };

        switch(arg.shape) {
            case Shape::Flag: break;
            case Shape::Count: ++node.count; break;
            case Shape::Value:
                for(auto value: values) {
                    auto read = text(value);
                    if(auto err = check_constraints(*arg.field, read)) {
                        return err;
                    }
                    if(arg.list) {
                        node.items.push_back(std::move(read));
                    } else {
                        node = std::move(read);
                    }
                }
                break;
            case Shape::Tuple: {
                Node tuple{.kind = Node::Kind::List, .begin = begin, .end = end};
                for(auto value: values) {
                    tuple.items.push_back(text(value));
                }
                if(arg.list) {
                    node.items.push_back(std::move(tuple));
                } else {
                    node = std::move(tuple);
                }
                break;
            }
            case Shape::Alias: std::unreachable();
        }
        if(node.kind == Node::Kind::List && !node.items.empty()) {
            node.begin = node.items.front().begin;
            node.end = node.items.back().end;
        } else if(node.kind != Node::Kind::Text) {
            node.begin = begin;
            node.end = end;
        }
        reading.given.push_back(Given{.path = level.prefix + arg.path,
                                      .values = std::vector(values.begin(), values.end())});
        return std::nullopt;
    }

    /// The error of `value`, a Text node, when the minimum, maximum or choices of `field`
    /// refuse it. A value that is no number is left to the decode, which says so.
    std::optional<Error> check_constraints(const meta::field_info& field, const Node& value) const {
        if(!field.choices.empty() &&
           std::ranges::find(field.choices, value.text) == field.choices.end()) {
            return error(Error::Kind::InvalidValue,
                         std::format("invalid value '{}' (supported: {})",
                                     value.text,
                                     join_names(field.choices)),
                         value.begin,
                         value.end);
        }
        const bool bounded = !std::holds_alternative<std::monostate>(field.minimum) ||
                             !std::holds_alternative<std::monostate>(field.maximum);
        if(!bounded) {
            return std::nullopt;
        }
        const auto number = number_of(value.text);
        if(meta::compare_numbers(number, field.minimum) == std::partial_ordering::less) {
            return error(Error::Kind::InvalidValue,
                         std::format("value {} is below the minimum {}",
                                     value.text,
                                     number_text(field.minimum)),
                         value.begin,
                         value.end);
        }
        if(meta::compare_numbers(number, field.maximum) == std::partial_ordering::greater) {
            return error(Error::Kind::InvalidValue,
                         std::format("value {} is above the maximum {}",
                                     value.text,
                                     number_text(field.maximum)),
                         value.begin,
                         value.end);
        }
        return std::nullopt;
    }

    /// The exclusive field whose alternative other than the one `arg` is in `level` holds, or
    /// no_exclusive.
    static std::uint32_t excluding(const Level& level, const Arg& arg) {
        const auto& command = *level.command;
        for(auto group = arg.group; group != no_group; group = command.groups[group].parent) {
            const auto& g = command.groups[group];
            if(g.exclusive == no_exclusive) {
                continue;
            }
            const Node* node = find_node(*level.root, command.exclusives[g.exclusive].keys);
            if(node != nullptr && node->count != g.alternative) {
                return g.exclusive;
            }
        }
        return no_exclusive;
    }

    /// The error of argument `arg_index`, read from [begin, end), given beside the argument
    /// that chose another alternative of exclusive field `exclusive`.
    Error conflict(const Level& level,
                   std::uint32_t arg_index,
                   std::uint32_t exclusive,
                   std::uint32_t begin,
                   std::uint32_t end) const {
        const auto& args = level.command->args;
        return error(Error::Kind::Conflict,
                     std::format("{} cannot be used with {}",
                                 args[arg_index].usage,
                                 args[level.chosen_by[exclusive]].usage),
                     begin,
                     end);
    }

    /// Makes the alternatives argument `arg_index` is in held by their exclusive fields, or says
    /// which argument of another alternative it conflicts with.
    std::optional<Error>
        choose(Level& level, std::uint32_t arg_index, std::uint32_t begin, std::uint32_t end) {
        const auto& command = *level.command;
        if(const auto other = excluding(level, command.args[arg_index]); other != no_exclusive) {
            return conflict(level, arg_index, other, begin, end);
        }
        for(auto group = command.args[arg_index].group; group != no_group;
            group = command.groups[group].parent) {
            const auto& g = command.groups[group];
            if(g.exclusive == no_exclusive) {
                continue;
            }
            Node& variant =
                make_node(*level.root, command.exclusives[g.exclusive].keys, Node::Kind::Variant);
            if(variant.items.empty()) {
                variant.count = g.alternative;
                variant.begin = begin;
                variant.end = end;
                variant.items.push_back(Node{.kind = Node::Kind::Object});
                level.chosen_by[g.exclusive] = arg_index;
            }
        }
        return std::nullopt;
    }

    /// Stores element `index` of `reading.active`, a positional argument, into `level`.
    std::optional<Error> store_input(Level& level, std::uint32_t index) {
        const auto& command = *level.command;
        const auto& spelling = reading.active[index];
        // The first positional argument of an alternative its exclusive field does not hold.
        std::optional<std::pair<std::uint32_t, std::uint32_t>> excluded;
        while(level.positional < command.positionals.size()) {
            const auto arg_index = command.positionals[level.positional];
            const auto& arg = command.args[arg_index];
            if(const auto other = excluding(level, arg); other != no_exclusive) {
                excluded = excluded.value_or(std::pair{arg_index, other});
                ++level.positional;
                continue;
            }
            if(!arg.list || level.occurrences[arg_index] < arg.spec.arg_num.max) {
                level.positional += arg.list ? 0 : 1;
                return store(level, arg_index, std::span(&spelling, 1), index, index + 1);
            }
            ++level.positional;
        }
        if(excluded) {
            return conflict(level, excluded->first, excluded->second, index, index + 1);
        }
        if(command.positionals.empty()) {
            return error(Error::Kind::UnexpectedArgument,
                         std::format("unexpected input argument {}", spelling),
                         index,
                         index + 1);
        }
        return error(Error::Kind::UnexpectedArgument,
                     std::format("unexpected input argument {}: expected {}",
                                 spelling,
                                 input_args_text(command.positional_count)),
                     index,
                     index + 1);
    }

    /// Replaces the alias at `index` of `reading.active`, which ends before `next`, with what it
    /// stands for.
    void expand(const Arg& arg, std::uint32_t index, std::uint32_t next) {
        std::vector<std::string_view> active(reading.active.begin(),
                                             reading.active.begin() + index);
        std::vector<std::uint32_t> origin(reading.origin.begin(), reading.origin.begin() + index);
        for(auto token: arg.spec.forward.names()) {
            active.push_back(reading.expansions.emplace_back(token));
            origin.push_back(reading.origin[index]);
        }
        active.insert(active.end(), reading.active.begin() + next, reading.active.end());
        origin.insert(origin.end(), reading.origin.begin() + next, reading.origin.end());
        reading.active = std::move(active);
        reading.origin = std::move(origin);
    }

    /// Whether the argv names one of `subcommands` at `level`.
    static bool given_subcommand(const Level& level, const Subcommands& subcommands) {
        return find_node(*level.root, subcommands.keys) != nullptr;
    }

    /// Checks what `level` requires once its arguments are read.
    std::optional<Error> check(const Level& level) const {
        const auto& command = *level.command;
        const auto at_end = level.cursor;
        auto present = [&](std::uint32_t group) {
            return detail::present(command, *level.root, group);
        };
        for(std::uint32_t i = 0; i < command.args.size(); ++i) {
            const auto& arg = command.args[i];
            const auto given = level.occurrences[i];
            if(arg.spec.positional && arg.list && given != 0 && given < arg.spec.arg_num.min) {
                return error(
                    Error::Kind::MissingRequired,
                    std::format("expected {}, got {}", input_args_text(arg.spec.arg_num), given),
                    at_end,
                    at_end);
            }
            if(given != 0 || !arg.required() || !present(arg.group)) {
                continue;
            }
            if(arg.spec.positional) {
                return error(Error::Kind::MissingRequired,
                             std::format("expected {}, got 0",
                                         input_args_text(arg.list ? arg.spec.arg_num : ArgNum(1))),
                             at_end,
                             at_end);
            }
            return error(Error::Kind::MissingRequired,
                         std::format("required option {} is missing", arg.usage),
                         at_end,
                         at_end);
        }
        for(const auto& exclusive: command.exclusives) {
            if(exclusive.required && present(exclusive.group) &&
               find_node(*level.root, exclusive.keys) == nullptr) {
                return error(
                    Error::Kind::MissingRequired,
                    std::format("one of {} is required", alternatives_text(command, exclusive)),
                    at_end,
                    at_end);
            }
        }
        if(command.subcommands && !command.subcommands->optional &&
           !given_subcommand(level, *command.subcommands) && present(command.subcommands->group)) {
            return error(Error::Kind::MissingSubcommand, "subcommand is required", at_end, at_end);
        }
        return std::nullopt;
    }

    /// Reads `reading.active` from `start` as `command`, its fields into `root`.
    std::optional<Error>
        run(const Command& command, Node& root, std::uint32_t start, std::string prefix = {}) {
        Level level{.command = &command,
                    .root = &root,
                    .prefix = std::move(prefix),
                    .occurrences = std::vector<std::uint32_t>(command.args.size()),
                    .chosen_by = std::vector<std::uint32_t>(command.exclusives.size(), no_arg),
                    .cursor = start};
        std::uint32_t base = start;
        while(true) {
            bool expanded = false;
            const auto rest = std::span(reading.active).subspan(base);
            for(const auto& result: command.table->parse(rest, command.parse_options)) {
                if(!result) {
                    const auto& err = result.error();
                    return error(err.kind, err.message, base + err.begin, base + err.end);
                }
                const auto& parsed = *result;
                const auto index = base + parsed.index;
                const auto next = base + parsed.next_index;
                const auto span_end = std::max(next, index + 1);
                if(parsed.id == command.table->unknown_option_id) {
                    auto err = error(Error::Kind::UnknownOption,
                                     std::format("unknown option '{}'", parsed.spelling),
                                     index,
                                     span_end);
                    // What precedes '=' is the name: "--verbos=1".
                    const auto name = std::string_view(parsed.spelling)
                                          .substr(0, std::string_view(parsed.spelling).find('='));
                    err.suggestion = closest(name, command.spellings);
                    return err;
                }
                if(parsed.id == command.table->input_option_id) {
                    if(parsed.spelling == "--" && command.parse_options.dash_dash_packing) {
                        if(auto err = store(level, *command.rest, parsed.values, index, span_end)) {
                            return err;
                        }
                        level.cursor = span_end;
                        continue;
                    }
                    if(command.subcommands) {
                        const auto& subcommands = *command.subcommands;
                        const auto found = std::ranges::find(subcommands.names, parsed.spelling);
                        if(found != subcommands.names.end()) {
                            const auto alternative =
                                static_cast<std::uint32_t>(found - subcommands.names.begin());
                            Node& variant =
                                make_node(*level.root, subcommands.keys, Node::Kind::Variant);
                            variant.count = alternative;
                            variant.begin = index;
                            variant.end = index + 1;
                            variant.items.push_back(Node{.kind = Node::Kind::Object});
                            reading.subcommand_words.push_back(index);
                            reading.given.push_back(Given{.path = level.prefix + subcommands.path,
                                                          .values = {reading.active[index]}});
                            if(auto err = run(*subcommands.commands[alternative],
                                              variant.items.front(),
                                              next,
                                              level.prefix + subcommands.path + ".")) {
                                return err;
                            }
                            // What the command itself requires is checked at the subcommand's
                            // word, unless the subcommand ended the parse.
                            level.cursor = index;
                            return stopped ? std::nullopt : check(level);
                        }
                        // A word no positional argument takes names a subcommand, even
                        // where one may be left out.
                        if(command.positionals.empty()) {
                            auto err =
                                error(Error::Kind::UnknownSubcommand,
                                      std::format("unknown subcommand '{}'", parsed.spelling),
                                      index,
                                      index + 1);
                            err.suggestion = closest(parsed.spelling, shown_names(subcommands));
                            return err;
                        }
                    }
                    if(auto err = store_input(level, index)) {
                        return err;
                    }
                    level.cursor = span_end;
                    continue;
                }

                const auto arg_index = command.arg_of[parsed.id];
                const auto& arg = command.args[arg_index];
                if(arg.shape == Shape::Alias) {
                    expand(arg, index, next);
                    base = index;
                    expanded = true;
                    break;
                }
                if(auto err = store(level, arg_index, parsed.values, index, span_end)) {
                    return err;
                }
                level.cursor = span_end;
                if(arg.spec.stop) {
                    stopped = true;
                    return std::nullopt;
                }
            }
            if(!expanded) {
                break;
            }
        }
        return check(level);
    }
};

}  // namespace

std::optional<std::string> read_bool(std::string_view text, bool& out) {
    if(text == "1" || iequals_ascii(text, "true") || iequals_ascii(text, "yes") ||
       iequals_ascii(text, "on")) {
        out = true;
        return std::nullopt;
    }
    if(text == "0" || iequals_ascii(text, "false") || iequals_ascii(text, "no") ||
       iequals_ascii(text, "off")) {
        out = false;
        return std::nullopt;
    }
    return std::format("invalid boolean value: {}", text);
}

namespace {

template <typename Integer>
std::optional<std::string> read_integer(std::string_view text, Integer& out) {
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    if(ec != std::errc() || end != text.data() + text.size()) {
        return std::format("invalid integer value: {}", text);
    }
    return std::nullopt;
}

}  // namespace

std::optional<std::string> read_number(std::string_view text, std::int64_t& out) {
    return read_integer(text, out);
}

std::optional<std::string> read_number(std::string_view text, std::uint64_t& out) {
    return read_integer(text, out);
}

std::optional<std::string> read_number(std::string_view text, double& out) {
    // strtod would skip leading spaces and read an empty text as 0; an integer takes neither.
    if(text.empty() || std::isspace(static_cast<unsigned char>(text.front()))) {
        return std::format("invalid floating-point value: {}", text);
    }
    const std::string copy(text);
    char* end = nullptr;
    errno = 0;
    out = std::strtod(copy.c_str(), &end);
    if(end != copy.c_str() + copy.size()) {
        return std::format("invalid floating-point value: {}", text);
    }
    if(errno == ERANGE) {
        return std::format("floating-point value out of range: {}", text);
    }
    return std::nullopt;
}

std::optional<Error> read(const Command& command, string_list_ref argv, Reading& reading) {
    ArgvReader reader(reading, argv);
    return reader.run(command, reading.root, 0);
}

Error decode_error(const Reading& reading,
                   string_list_ref argv,
                   const Node* last,
                   const codec::rich_error& error) {
    // Past the active elements when the value is the struct's own, left out of the argv.
    const auto past = static_cast<std::uint32_t>(reading.active.size());
    return error_from(reading,
                      argv,
                      Error::Kind::InvalidValue,
                      error.message,
                      last ? last->begin : past,
                      last ? last->end : past);
}

}  // namespace kotatsu::cli::detail
