#include "kotatsu/cli/to_argv.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "command.h"

namespace kotatsu::cli::detail {

namespace {

/// The node `keys` reach from `root`, or null for one the options leave empty.
const Node* node_at(const Node& root, std::span<const std::string_view> keys) {
    const Node* node = find_node(root, keys);
    // An empty optional writes nothing.
    return node && node->kind == Node::Kind::Flag ? nullptr : node;
}

/// The values of `node`: its items for a list, else itself.
std::vector<const Node*> values_of(const Node& node) {
    std::vector<const Node*> values;
    if(node.kind == Node::Kind::List) {
        for(const auto& item: node.items) {
            values.push_back(&item);
        }
    } else {
        values.push_back(&node);
    }
    return values;
}

void write(const Command& command, const Node& root, std::vector<std::string>& out) {
    // The subcommand the options hold, before which only the arguments that go with it are
    // written, and those that end the parse ahead of it.
    const Node* subcommand =
        command.subcommands ? node_at(root, command.subcommands->keys) : nullptr;
    // An argument of an alternative the options do not hold writes nothing.
    auto writes = [&](const Arg& arg) {
        return (subcommand == nullptr || arg.global || arg.spec.stop) &&
               present(command, root, arg.group);
    };
    for(const auto& arg: command.args) {
        if(arg.spec.positional || arg.spec.rest || !writes(arg)) {
            continue;
        }
        const Node* node = node_at(root, arg.keys);
        if(node == nullptr) {
            continue;
        }
        switch(arg.shape) {
            case Shape::Alias: break;
            case Shape::Flag:
                if(node->text == "true") {
                    out.push_back(arg.write_name);
                }
                break;
            case Shape::Count: {
                std::uint32_t count = 0;
                std::from_chars(node->text.data(), node->text.data() + node->text.size(), count);
                out.insert(out.end(), count, arg.write_name);
                break;
            }
            case Shape::Tuple: {
                const auto tuples = arg.list ? values_of(*node) : std::vector{node};
                for(const auto* tuple: tuples) {
                    out.push_back(arg.write_name);
                    for(const auto& element: tuple->items) {
                        out.emplace_back(element.text);
                    }
                }
                break;
            }
            case Shape::Value: {
                const auto values = values_of(*node);
                if(arg.spec.comma) {
                    // A comma list gives its values in one argument.
                    if(!values.empty()) {
                        std::string token = arg.write_name;
                        for(std::size_t i = 0; i < values.size(); ++i) {
                            token += i == 0 ? "" : ",";
                            token += values[i]->text;
                        }
                        out.push_back(std::move(token));
                    }
                    break;
                }
                // A list gives one occurrence a value.
                for(const auto* value: values) {
                    switch(arg.form) {
                        case Form::Equals:
                            out.push_back(std::format("{}={}", arg.write_name, value->text));
                            break;
                        case Form::Joined:
                            out.push_back(std::format("{}{}", arg.write_name, value->text));
                            break;
                        case Form::Separate:
                            out.push_back(arg.write_name);
                            out.emplace_back(value->text);
                            break;
                    }
                }
                break;
            }
        }
    }
    std::vector<std::string_view> positionals;
    for(const auto index: command.positionals) {
        if(!writes(command.args[index])) {
            continue;
        }
        if(const Node* node = node_at(root, command.args[index].keys)) {
            for(const auto* value: values_of(*node)) {
                positionals.push_back(value->text);
            }
        }
    }
    // A positional argument an option might be taken for follows "--", unless "--" starts the
    // rest.
    if(!command.rest && std::ranges::any_of(positionals, [](std::string_view value) {
           return value.starts_with('-');
       })) {
        out.emplace_back("--");
    }
    out.insert(out.end(), positionals.begin(), positionals.end());
    if(subcommand != nullptr) {
        const auto& subcommands = *command.subcommands;
        // A variant tagged by name writes the name of the alternative, not its index.
        const auto alternative =
            subcommand->kind == Node::Kind::Variant
                ? subcommand->count
                : static_cast<std::uint32_t>(
                      std::ranges::find(subcommands.names, subcommand->keys.front()) -
                      subcommands.names.begin());
        out.push_back(subcommands.names[alternative]);
        write(*subcommands.commands[alternative], subcommand->items.front(), out);
        // What follows "--" would be the subcommand's.
        return;
    }
    if(command.rest && present(command, root, command.args[*command.rest].group)) {
        const auto& rest = command.args[*command.rest];
        const Node* node = node_at(root, rest.keys);
        const bool given = node != nullptr && !node->items.empty();
        // "--" alone picks the alternative holding the rest, which nothing else might.
        if(given || rest.alternative != no_group) {
            out.emplace_back("--");
        }
        if(given) {
            for(const auto* value: values_of(*node)) {
                out.emplace_back(value->text);
            }
        }
    }
}

}  // namespace

std::vector<std::string> write_args(const Command& command, const Node& root) {
    std::vector<std::string> out;
    write(command, root, out);
    return out;
}

}  // namespace kotatsu::cli::detail
