#include "kotatsu/cli/usage.h"

#include <algorithm>
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
       arg.required() || !present(command, defaults, arg.group)) {
        return {};
    }
    const Node* node = find_node(defaults, arg.keys);
    if(node == nullptr || node->kind == Node::Kind::Flag) {
        return {};
    }
    return arg.list ? join(node->items | std::views::transform(text_of), ", ") : text_of(*node);
}

}  // namespace

std::vector<UsageEntry> usage_entries(const Command& command, const Node& defaults) {
    std::vector<UsageEntry> entries;
    for(const auto& arg: command.args) {
        if(arg.spec.hidden) {
            continue;
        }
        entries.push_back(UsageEntry{
            .kind = arg.spec.positional || arg.spec.rest ? UsageEntry::Kind::Positional
                                                         : UsageEntry::Kind::Option,
            .usage = arg.usage,
            .help = std::string(arg.help),
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

std::string usage(const Command& command,
                  const Node& defaults,
                  std::string_view usage_line,
                  const Style& style) {
    const auto entries = usage_entries(command, defaults);
    std::string out;
    if(!command.type->description.empty()) {
        out += style.color ? paint(ansi_bold, command.type->description)
                           : std::string(command.type->description);
        out += "\n\n";
    }
    out += style.color
               ? std::format("{} {}", paint(ansi_heading, "Usage"), paint(ansi_bold, usage_line))
               : std::format("usage: {}", usage_line);
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
