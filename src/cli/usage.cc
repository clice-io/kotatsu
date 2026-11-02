#include "kotatsu/cli/usage.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ansi.h"
#include "command.h"

namespace kotatsu::cli::detail {

namespace {

/// The heading of the arguments of `group`: the description of the innermost group around
/// them that has one, else none.
std::string_view heading(const Command& command, std::uint32_t group) {
    while(group != no_group) {
        const auto& g = command.groups[group];
        if(!g.description.empty()) {
            return g.description;
        }
        group = g.parent;
    }
    return {};
}

/// `usage`, then `help` at the help column, on a line of its own when the usage reaches it.
std::string lay_out(std::string_view usage,
                    std::size_t width,
                    std::string_view help,
                    std::size_t help_column) {
    if(width >= help_column) {
        return std::format("  {}\n  {:{}}{}", usage, "", help_column, help);
    }
    return std::format("  {}{:{}}{}", usage, "", help_column - width, help);
}

std::string help_of(const UsageEntry& entry) {
    std::string help = entry.help.empty() ? "no description provided" : entry.help;
    if(entry.repeatable) {
        help += " (may be repeated)";
    }
    if(!entry.default_value.empty()) {
        help += std::format(" [default: {}]", entry.default_value);
    }
    if(!entry.range.empty()) {
        help += std::format(" [range: {}]", entry.range);
    }
    return help;
}

/// The numbers `field` takes: "1..64", "1..", "..64"; empty when unbounded.
std::string range_of(const codec::field_info& field) {
    const auto minimum = number_text(field.minimum);
    const auto maximum = number_text(field.maximum);
    return minimum.empty() && maximum.empty() ? "" : std::format("{}..{}", minimum, maximum);
}

/// The text of a value node as an argv gives it: a tuple's elements joined by ' '.
std::string text_of(const Node& node) {
    if(node.kind != Node::Kind::List) {
        return std::string(node.text);
    }
    return join(node.items | std::views::transform(&Node::text), " ");
}

/// The default of `arg` as `defaults` holds it: empty for a flag, a count, an alias, an empty
/// list, an empty optional and an alternative the default does not hold.
std::string default_of(const Command& command, const Arg& arg, const Node& defaults) {
    // A required argument has no default: the argv gives it.
    if(arg.shape == Shape::Flag || arg.shape == Shape::Count || arg.shape == Shape::Alias ||
       arg.required || !present(command, defaults, arg.group)) {
        return {};
    }
    const Node* node = find_node(defaults, arg.keys);
    if(node == nullptr || node->kind == Node::Kind::Flag) {
        return {};
    }
    return arg.list ? join(node->items | std::views::transform(text_of), ", ") : text_of(*node);
}

/// The subcommand `word` names of those `command` takes, by index; a word naming none ends the
/// program, as the caller passed subcommands its options do not have.
std::uint32_t subcommand_index(const Command& command, std::string_view word) {
    if(command.subcommands) {
        const auto& names = command.subcommands->names;
        const auto found = std::ranges::find(names, word);
        if(found != names.end()) {
            return static_cast<std::uint32_t>(found - names.begin());
        }
    }
    std::fprintf(stderr,
                 "cli: a usage asked for subcommand %.*s, which its command does not take\n",
                 static_cast<int>(word.size()),
                 word.data());
    std::abort();
}

/// Whether the arguments of `group` are there whenever those of `outer`, a group around it, are:
/// no optional group and no alternative of an exclusive field is between them.
bool unconditional(const Command& command, std::uint32_t group, std::uint32_t outer = no_group) {
    for(; group != outer; group = command.groups[group].parent) {
        const auto& g = command.groups[group];
        if(g.optional || g.exclusive != no_exclusive) {
            return false;
        }
    }
    return true;
}

/// How a usage line writes `arg` when the arguments of `outer` are there: a positional argument
/// or the rest, in brackets unless required; an option when required; else nothing.
std::string line_part(const Command& command, const Arg& arg, std::uint32_t outer) {
    const bool required = arg.required && unconditional(command, arg.group, outer);
    if(!arg.spec.positional && !arg.spec.rest) {
        return required ? arg.synopsis : "";
    }
    return required ? arg.synopsis : std::format("[{}]", arg.synopsis);
}

/// How a usage line writes exclusive field `index`: its alternatives, each by its positional
/// arguments, the rest and its required options, else by its first argument; "(...)" when it is
/// required, "[...]" when one of them takes positional arguments, and nothing otherwise.
std::string exclusive_part(const Command& command, std::uint32_t index) {
    const auto& exclusive = command.exclusives[index];
    bool positional = false;
    std::vector<std::string> alternatives;
    for(std::uint32_t i = 0; i < exclusive.alternatives.size(); ++i) {
        const auto alternative = exclusive.alternatives[i];
        std::vector<std::string> parts;
        for(const auto& arg: command.args) {
            if(arg.alternative != alternative || arg.spec.hidden) {
                continue;
            }
            positional = positional || arg.spec.positional || arg.spec.rest;
            if(auto part = line_part(command, arg, alternative); !part.empty()) {
                parts.push_back(std::move(part));
            }
        }
        if(parts.empty() && exclusive.first_args[i] != no_arg) {
            parts.push_back(command.args[exclusive.first_args[i]].synopsis);
        }
        if(!parts.empty()) {
            alternatives.push_back(join(parts, " "));
        }
    }
    const auto text = join(alternatives, " | ");
    if(exclusive.required && unconditional(command, exclusive.group)) {
        return std::format("({})", text);
    }
    return positional ? std::format("[{}]", text) : "";
}

/// The line that calls `command`, reached by `words` in `program`: "tool serve <PATH> [OPTIONS]".
std::string usage_line(const Command& command, std::string_view program, string_list_ref words) {
    std::vector<std::string> parts = {std::string(program)};
    for(const auto word: words) {
        parts.emplace_back(word);
    }
    bool options = !command.help_usage.empty();
    std::vector<bool> written(command.exclusives.size());
    for(const auto& arg: command.args) {
        if(arg.spec.hidden) {
            continue;
        }
        options = options || (!arg.spec.positional && !arg.spec.rest);
        std::string part;
        if(arg.alternative != no_group) {
            const auto exclusive = command.groups[arg.alternative].exclusive;
            if(!written[exclusive]) {
                written[exclusive] = true;
                part = exclusive_part(command, exclusive);
            }
        } else if(!arg.spec.rest) {
            part = line_part(command, arg, no_group);
        }
        if(!part.empty()) {
            parts.push_back(std::move(part));
        }
    }
    if(options) {
        parts.emplace_back("[OPTIONS]");
    }
    if(command.rest && command.args[*command.rest].alternative == no_group &&
       !command.args[*command.rest].spec.hidden) {
        parts.push_back(line_part(command, command.args[*command.rest], no_group));
    }
    if(command.subcommands) {
        const auto& subcommands = *command.subcommands;
        const std::string placeholder =
            subcommands.meta_var.empty() ? "<command>" : std::string(subcommands.meta_var);
        parts.push_back(!subcommands.optional && unconditional(command, subcommands.group)
                            ? placeholder
                            : std::format("[{}]", placeholder));
    }
    return join(parts, " ");
}

}  // namespace

Node subcommand_path(const Command& command, string_list_ref subcommands) {
    Node root;
    Node* node = &root;
    const Command* at = &command;
    for(const auto word: subcommands) {
        const auto index = subcommand_index(*at, word);
        Node& variant = make_node(*node, at->subcommands->keys, Node::Kind::Variant);
        variant.count = index;
        node = &variant.items.emplace_back(Node{.kind = Node::Kind::Object});
        at = at->subcommands->commands[index].get();
    }
    return root;
}

std::vector<UsageEntry> usage_entries(const Command& command, const Node& defaults) {
    std::vector<UsageEntry> entries;
    if(!command.help_usage.empty()) {
        entries.push_back(UsageEntry{
            .kind = UsageEntry::Kind::Option,
            .usage = command.help_usage,
            .help = "display this help and exit",
        });
    }
    for(const auto& arg: command.args) {
        if(arg.spec.hidden) {
            continue;
        }
        entries.push_back(UsageEntry{
            .kind = arg.spec.positional || arg.spec.rest ? UsageEntry::Kind::Positional
                                                         : UsageEntry::Kind::Option,
            .usage = arg.usage,
            .help = std::string(arg.field->description),
            .group = std::string(heading(command, arg.group)),
            .repeatable =
                !arg.spec.positional && !arg.spec.rest && (arg.list || arg.shape == Shape::Count),
            .default_value = default_of(command, arg, defaults),
            .range = range_of(*arg.field),
        });
    }
    if(command.subcommands) {
        const auto& subcommands = *command.subcommands;
        for(std::size_t i = 0; i < subcommands.names.size(); ++i) {
            if(subcommands.hidden[i]) {
                continue;
            }
            entries.push_back(UsageEntry{
                .kind = UsageEntry::Kind::Subcommand,
                .usage = subcommands.names[i],
                .help = std::string(subcommands.descriptions[i]),
            });
        }
    }
    return entries;
}

std::string usage(const Command& root,
                  const Node& defaults,
                  std::string_view program,
                  string_list_ref subcommands,
                  const Style& style) {
    // The command the words name, and its options as `defaults` holds them: none when they hold
    // no defaults at all.
    const Node none;
    const Command* at = &root;
    const Node* held = &defaults;
    for(const auto word: subcommands) {
        const auto index = subcommand_index(*at, word);
        const Node* variant = find_node(*held, at->subcommands->keys);
        held = variant != nullptr ? &variant->items.front() : &none;
        at = at->subcommands->commands[index].get();
    }
    const auto& command = *at;
    const auto synopsis = usage_line(command, program, subcommands);

    const auto entries = usage_entries(command, *held);
    std::string out;
    if(!command.type->description.empty()) {
        out += style.color ? paint(ansi_bold, command.type->description)
                           : std::string(command.type->description);
        out += "\n\n";
    }
    out += style.color
               ? std::format("{} {}", paint(ansi_heading, "Usage"), paint(ansi_bold, synopsis))
               : std::format("usage: {}", synopsis);
    out += "\n";

    auto line = [&](const UsageEntry& entry) {
        const auto syntax = style.color ? paint(ansi_syntax, entry.usage) : entry.usage;
        const auto help = style.color ? paint(ansi_help, help_of(entry)) : help_of(entry);
        out += lay_out(syntax, entry.usage.size(), help, style.help_column);
        out += "\n";
    };

    // The options of the command itself, then each group with a heading of its own, in the
    // order they first appear.
    std::vector<std::string_view> headings;
    for(const auto& entry: entries) {
        if(entry.kind != UsageEntry::Kind::Subcommand &&
           std::ranges::find(headings, entry.group) == headings.end()) {
            headings.push_back(entry.group);
        }
    }
    std::ranges::stable_partition(headings, [](std::string_view h) { return h.empty(); });
    for(auto title: headings) {
        out += "\n";
        if(title.empty()) {
            out += style.color ? paint(ansi_heading, "Options") : "Options:";
        } else {
            out +=
                style.color
                    ? std::format("  {} {}", paint(ansi_group, "◆"), paint(ansi_group_title, title))
                    : std::format("{}:", title);
        }
        out += "\n";
        for(const auto& entry: entries) {
            if(entry.kind != UsageEntry::Kind::Subcommand && entry.group == title) {
                line(entry);
            }
        }
    }

    if(command.subcommands) {
        out += "\n";
        out += style.color ? paint(ansi_heading, "Commands") : "Subcommands:";
        out += "\n";
        std::size_t width = 0;
        for(const auto& entry: entries) {
            if(entry.kind == UsageEntry::Kind::Subcommand) {
                width = std::max(width, entry.usage.size());
            }
        }
        for(const auto& entry: entries) {
            if(entry.kind != UsageEntry::Kind::Subcommand) {
                continue;
            }
            const auto name = style.color ? paint(ansi_syntax, entry.usage) : entry.usage;
            if(entry.help.empty()) {
                out += std::format("  {}\n", name);
            } else {
                out += std::format("  {}{:{}}{}\n",
                                   name,
                                   "",
                                   width - entry.usage.size() + 2,
                                   style.color ? paint(ansi_help, entry.help) : entry.help);
            }
        }
    }
    return out;
}

}  // namespace kotatsu::cli::detail
