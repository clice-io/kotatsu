#include "kotatsu/cli/to_argv.h"

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "command.h"

namespace kotatsu::cli::detail {

namespace {

/// The node `keys` reach from `root`, or null for one the options leave empty.
const Node* node_at(const Node& root, const std::vector<std::string_view>& keys) {
    const Node* node = &root;
    for(auto key: keys) {
        if(node->kind != Node::Kind::Object) {
            return nullptr;
        }
        const Node* found = nullptr;
        for(std::size_t i = 0; i < node->keys.size(); ++i) {
            if(node->keys[i] == key) {
                found = &node->items[i];
                break;
            }
        }
        if(found == nullptr) {
            return nullptr;
        }
        node = found;
    }
    // A false flag and an empty optional write nothing.
    if(node->kind == Node::Kind::Flag && node->count == 0) {
        return nullptr;
    }
    return node;
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
    for(const auto& arg: command.args) {
        if(arg.positional || arg.spec.rest) {
            continue;
        }
        const Node* node = node_at(root, arg.keys);
        if(node == nullptr) {
            continue;
        }
        switch(arg.shape) {
            case Shape::Alias: break;
            case Shape::Flag: out.push_back(arg.write_name); break;
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
    for(const auto index: command.positionals) {
        if(const Node* node = node_at(root, command.args[index].keys)) {
            for(const auto* value: values_of(*node)) {
                out.emplace_back(value->text);
            }
        }
    }
    if(command.subcommands) {
        const auto& subcommands = *command.subcommands;
        if(const Node* node = node_at(root, subcommands.keys)) {
            out.push_back(subcommands.names[node->count]);
            write(*subcommands.commands[node->count], node->items.front(), out);
            // What follows "--" would be the subcommand's.
            return;
        }
    }
    if(command.rest) {
        if(const Node* node = node_at(root, command.args[*command.rest].keys);
           node != nullptr && !node->items.empty()) {
            out.emplace_back("--");
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
