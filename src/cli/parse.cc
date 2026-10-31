#include "kotatsu/cli/parse.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "command.h"
#include "kotatsu/cli/table/table.h"
#include "kotatsu/support/naming.h"

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

/// The node of `object` under `key`, made empty of `kind` when it has none.
Node& child(Node& object, std::string_view key, Node::Kind kind) {
    for(std::size_t i = 0; i < object.keys.size(); ++i) {
        if(object.keys[i] == key) {
            return object.items[i];
        }
    }
    object.keys.push_back(key);
    return object.items.emplace_back(Node{.kind = kind});
}

/// The node of `object` under `key`, or null.
const Node* find_child(const Node& object, std::string_view key) {
    for(std::size_t i = 0; i < object.keys.size(); ++i) {
        if(object.keys[i] == key) {
            return &object.items[i];
        }
    }
    return nullptr;
}

/// Reads one argv against a command, and the subcommands it names.
class ArgvReader {
    Reading& reading;
    string_list_ref argv;

    /// The elements of `argv` [begin, end) of `reading.active` come from.
    std::pair<std::uint32_t, std::uint32_t> origin(std::uint32_t begin, std::uint32_t end) const {
        const auto size = static_cast<std::uint32_t>(reading.active.size());
        const auto user_size = static_cast<std::uint32_t>(argv.size());
        const auto first = begin >= size ? user_size : reading.origin[begin];
        const auto last = end == 0 || end <= begin ? first
                          : end > size             ? user_size
                                                   : reading.origin[end - 1] + 1;
        return {first, std::max(first, last)};
    }

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
        const auto [first, last] = origin(begin, end);
        return error_at(kind, std::move(message), argv, first, last);
    }

    /// The state of a command being read.
    struct Level {
        const Command* command;
        Node* root;
        /// The occurrences of each argument of the command so far.
        std::vector<std::uint32_t> occurrences;
        /// The positional argument the next input goes to.
        std::size_t positional = 0;
        /// Where the last argument read ended, in `reading.active`.
        std::uint32_t cursor = 0;
    };

    /// The node argument `arg` of `level` stores into: its field's under the objects of the
    /// groups around it, made as needed.
    Node& node_of(Level& level, const Arg& arg) {
        Node* node = level.root;
        for(std::size_t i = 0; i + 1 < arg.keys.size(); ++i) {
            node = &child(*node, arg.keys[i], Node::Kind::Object);
        }
        const auto kind = arg.shape == Shape::Flag                ? Node::Kind::Flag
                          : arg.shape == Shape::Count             ? Node::Kind::Count
                          : arg.list || arg.shape == Shape::Tuple ? Node::Kind::List
                                                                  : Node::Kind::Text;
        return child(*node, arg.keys.back(), kind);
    }

    /// Where `value`, one of the values of `parsed`, is in `reading.active`: its own element
    /// when it is one, else the argument's.
    std::pair<std::uint32_t, std::uint32_t> value_span(const ParsedArg& parsed,
                                                       std::string_view value,
                                                       std::uint32_t base) const {
        const auto begin = base + parsed.index;
        const auto end = base + std::max(parsed.next_index, parsed.index + 1);
        for(auto i = begin; i < end; ++i) {
            if(reading.active[i].data() == value.data() &&
               reading.active[i].size() == value.size()) {
                return {i, i + 1};
            }
        }
        return {begin, begin + 1};
    }

    /// Stores `parsed` into argument `arg_index` of `level`.
    void store(Level& level, std::uint32_t arg_index, const ParsedArg& parsed, std::uint32_t base) {
        const auto& arg = level.command->args[arg_index];
        ++level.occurrences[arg_index];
        Node& node = node_of(level, arg);
        const auto begin = base + parsed.index;
        const auto end = base + std::max(parsed.next_index, parsed.index + 1);
        auto text = [&](std::string_view value) {
            const auto [first, last] = value_span(parsed, value, base);
            return Node{.kind = Node::Kind::Text, .text = value, .begin = first, .end = last};
        };

        Given given{.path = arg.path};
        switch(arg.shape) {
            case Shape::Flag: break;
            case Shape::Count: ++node.count; break;
            case Shape::Value:
                given.values.assign(parsed.values.begin(), parsed.values.end());
                if(arg.list) {
                    for(auto value: parsed.values) {
                        node.items.push_back(text(value));
                    }
                } else {
                    node = text(parsed.values.front());
                }
                break;
            case Shape::Tuple: {
                given.values.assign(parsed.values.begin(), parsed.values.end());
                Node tuple{.kind = Node::Kind::List};
                for(auto value: parsed.values) {
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
        node.begin = node.items.empty() && node.kind != Node::Kind::Text ? begin : node.begin;
        node.end = node.items.empty() && node.kind != Node::Kind::Text ? end : node.end;
        if(node.kind == Node::Kind::List && !node.items.empty()) {
            node.begin = node.items.front().begin;
            node.end = node.items.back().end;
        }
        reading.given.push_back(std::move(given));
    }

    /// Stores positional argument `spelling`, element `index` of `reading.active`, into `level`.
    std::optional<Error> store_input(Level& level, std::uint32_t index) {
        const auto& command = *level.command;
        const auto spelling = reading.active[index];
        while(level.positional < command.positionals.size()) {
            const auto arg_index = command.positionals[level.positional];
            const auto& arg = command.args[arg_index];
            if(!arg.list) {
                ++level.positional;
                ++level.occurrences[arg_index];
                child_text(level, arg, spelling, index);
                return std::nullopt;
            }
            if(level.occurrences[arg_index] < arg.spec.arg_num.max) {
                ++level.occurrences[arg_index];
                Node& node = node_of(level, arg);
                node.items.push_back(Node{.kind = Node::Kind::Text,
                                          .text = spelling,
                                          .begin = index,
                                          .end = index + 1});
                node.begin = node.items.front().begin;
                node.end = index + 1;
                reading.given.push_back(Given{.path = arg.path, .values = {spelling}});
                return std::nullopt;
            }
            ++level.positional;
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

    void child_text(Level& level, const Arg& arg, std::string_view spelling, std::uint32_t index) {
        Node& node = node_of(level, arg);
        node = Node{.kind = Node::Kind::Text, .text = spelling, .begin = index, .end = index + 1};
        reading.given.push_back(Given{.path = arg.path, .values = {spelling}});
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
        const Node* node = level.root;
        for(auto key: subcommands.keys) {
            node = node ? find_child(*node, key) : nullptr;
        }
        return node != nullptr;
    }

    /// Checks what `level` requires once its arguments are read.
    std::optional<Error> check(const Level& level) const {
        const auto& command = *level.command;
        const auto at_end = level.cursor;
        auto present = [&](std::uint32_t group) {
            // A group is there when the argv gives it something, or when it is no optional
            // and the group around it is there.
            while(group != no_group) {
                const auto& g = command.groups[group];
                if(g.optional) {
                    const Node* node = level.root;
                    for(auto key: g.keys) {
                        node = node ? find_child(*node, key) : nullptr;
                    }
                    return node != nullptr;
                }
                group = g.parent;
            }
            return true;
        };
        for(std::uint32_t i = 0; i < command.args.size(); ++i) {
            const auto& arg = command.args[i];
            const auto given = level.occurrences[i];
            if(arg.positional && arg.list && given != 0 && given < arg.spec.arg_num.min) {
                return error(
                    Error::Kind::MissingRequired,
                    std::format("expected {}, got {}", input_args_text(arg.spec.arg_num), given),
                    at_end,
                    at_end);
            }
            if(given != 0 || !arg.required() || !present(arg.group)) {
                continue;
            }
            if(arg.positional) {
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
        if(command.subcommands && !command.subcommands->optional &&
           !given_subcommand(level, *command.subcommands) && present(command.subcommands->group)) {
            return error(Error::Kind::MissingSubcommand, "subcommand is required", at_end, at_end);
        }
        return std::nullopt;
    }

    /// Reads `reading.active` from `start` as `command`, its fields into `root`.
    std::optional<Error> run(const Command& command, Node& root, std::uint32_t start) {
        Level level{.command = &command,
                    .root = &root,
                    .occurrences = std::vector<std::uint32_t>(command.args.size()),
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
                    return error(Error::Kind::UnknownOption,
                                 std::format("unknown option '{}'", parsed.spelling),
                                 index,
                                 span_end);
                }
                if(parsed.id == command.table->input_option_id) {
                    if(parsed.spelling == "--" && command.parse_options.dash_dash_packing) {
                        const auto& arg = command.args[*command.rest];
                        ++level.occurrences[*command.rest];
                        Node& node = node_of(level, arg);
                        Given given{.path = arg.path};
                        for(std::uint32_t i = 0; i < parsed.values.size(); ++i) {
                            node.items.push_back(Node{.kind = Node::Kind::Text,
                                                      .text = parsed.values[i],
                                                      .begin = index + 1 + i,
                                                      .end = index + 2 + i});
                            given.values.push_back(parsed.values[i]);
                        }
                        node.begin = index;
                        node.end = span_end;
                        reading.given.push_back(std::move(given));
                        level.cursor = span_end;
                        continue;
                    }
                    if(command.subcommands) {
                        const auto& subcommands = *command.subcommands;
                        const auto found = std::ranges::find(subcommands.names, parsed.spelling);
                        if(found != subcommands.names.end()) {
                            const auto alternative =
                                static_cast<std::uint32_t>(found - subcommands.names.begin());
                            Node* parent = level.root;
                            for(std::size_t i = 0; i + 1 < subcommands.keys.size(); ++i) {
                                parent = &child(*parent, subcommands.keys[i], Node::Kind::Object);
                            }
                            Node& variant =
                                child(*parent, subcommands.keys.back(), Node::Kind::Variant);
                            variant.count = alternative;
                            variant.begin = index;
                            variant.end = index + 1;
                            variant.items.push_back(Node{.kind = Node::Kind::Object});
                            level.cursor = next;
                            if(auto err = check(level)) {
                                return err;
                            }
                            reading.given.push_back(
                                Given{.path = subcommands.path, .values = {parsed.spelling}});
                            return run(*subcommands.commands[alternative],
                                       variant.items.front(),
                                       next);
                        }
                        if(!subcommands.optional && command.positionals.empty()) {
                            return error(Error::Kind::UnknownSubcommand,
                                         std::format("unknown subcommand '{}'", parsed.spelling),
                                         index,
                                         index + 1);
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
                store(level, arg_index, parsed, base);
                level.cursor = span_end;
                if(arg.spec.stop) {
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

template <typename Integer>
static std::optional<std::string> read_integer(std::string_view text, Integer& out) {
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    if(ec != std::errc() || end != text.data() + text.size()) {
        return std::format("invalid integer value: {}", text);
    }
    return std::nullopt;
}

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
    const auto size = static_cast<std::uint32_t>(reading.active.size());
    std::uint32_t begin = size;
    std::uint32_t end = size;
    if(last != nullptr) {
        begin = last->begin;
        end = last->end;
    }
    const auto user_size = static_cast<std::uint32_t>(argv.size());
    const auto first = begin >= size ? user_size : reading.origin[begin];
    const std::uint32_t past = end <= begin ? first
                               : end > size ? user_size
                                            : reading.origin[end - 1] + 1;
    return error_at(Error::Kind::InvalidValue, error.message, argv, first, std::max(first, past));
}

}  // namespace kotatsu::cli::detail
