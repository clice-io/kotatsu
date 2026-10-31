#include "command.h"

#include <algorithm>
#include <format>
#include <map>
#include <mutex>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kotatsu/cli/parse.h"

namespace kotatsu::cli::detail {

namespace {

/// The prefixes an option named `name` accepts: "--name" takes "--", "-n" takes "-", and
/// "/name" takes "/" or "-". The name is checked when its struct is declared.
std::span<const std::string_view> prefixes_of(std::string_view name) {
    if(name.starts_with("--")) {
        return pfx_double;
    }
    return name.starts_with('-') ? pfx_dash : pfx_slash_dash;
}

/// What a usage shows for a value of `type` declared with `spec`: its meta_var, else the names
/// of an enum, else "<value>".
std::string value_token(const ArgSpec& spec, const meta::type_info& type) {
    if(!spec.meta_var.empty()) {
        return std::string(spec.meta_var);
    }
    if(type.kind == meta::type_kind::enumeration) {
        const auto names = static_cast<const meta::enum_type_info&>(type).member_names;
        if(!names.empty()) {
            constexpr std::size_t shown = 6;
            std::string token = "<";
            for(std::size_t i = 0; i < std::min(names.size(), shown); ++i) {
                token += i == 0 ? "" : "|";
                token += kebab(names[i]);
            }
            token += names.size() > shown ? "|...>" : ">";
            return token;
        }
    }
    return "<value>";
}

/// `token` once for each of `count` values: "<X1> <X2>"; a token naming each value itself,
/// "<FROM> <TO>", or listing choices, "<a|b>", as written.
std::string numbered(std::string_view token, std::uint32_t count) {
    if(count <= 1 || token.find(' ') != std::string_view::npos) {
        return std::string(token);
    }
    std::string base(token);
    if(base.size() >= 2 && base.front() == '<' && base.back() == '>') {
        base = base.substr(1, base.size() - 2);
    }
    std::string numbered_tokens;
    for(std::uint32_t i = 1; i <= count; ++i) {
        numbered_tokens += i == 1 ? "" : " ";
        numbered_tokens += token.find('|') != std::string_view::npos
                               ? std::string(token)
                               : std::format("<{}{}>", base, i);
    }
    return numbered_tokens;
}

std::string join(const std::vector<std::string>& parts, std::string_view separator) {
    std::string joined;
    for(const auto& part: parts) {
        joined += joined.empty() ? "" : separator;
        joined += part;
    }
    return joined;
}

/// Builds a command by the rules declaration.h states, which check_declaration() has held its
/// struct to.
class Builder {
    Command& command;
    /// The '=' forms of the names, named once every field has named its own.
    std::vector<std::pair<NameForm, std::uint32_t>> equals_forms;

    void add_option(const NameForm& form, std::uint32_t arg) {
        const auto id = static_cast<std::uint32_t>(command.options.size() + 1);
        const auto& spelling = command.spellings.emplace_back(form.name);
        command.options.push_back(
            Option::unaliased_one(prefixes_of(spelling), spelling, id, form.kind, 0, "", ""));
        if(form.kind == OptionKind::MultiArg) {
            command.options.back().num_args =
                static_cast<unsigned char>(command.args[arg].tuple_size);
        }
        command.arg_of.push_back(arg);
    }

    /// Names the '=' forms no option is named itself; an argument whose own '=' form yields
    /// writes its value separate.
    void name_equals_forms() {
        for(const auto& [form, index]: equals_forms) {
            if(std::ranges::find(command.spellings, form.name) == command.spellings.end()) {
                add_option(form, index);
                continue;
            }
            auto& arg = command.args[index];
            if(arg.form == Form::Equals && arg.write_name + "=" == form.name) {
                arg.form = Form::Separate;
            }
        }
    }

    /// Registers named argument `index` under its names, and says how a usage and to_argv()
    /// write it.
    void name(std::uint32_t index) {
        auto& arg = command.args[index];
        const auto forms = name_forms(arg, arg.field->declared_name);
        const auto token = value_token(arg.spec, *arg.value_type);
        std::vector<std::string> own;
        std::vector<std::string> separate;
        std::vector<std::string> joined;
        for(const auto& form: forms) {
            switch(form.role) {
                case NameForm::Role::Own:
                    add_option(form, index);
                    own.push_back(form.name);
                    if(form.kind == OptionKind::Separate) {
                        separate.push_back(form.name);
                    } else if(form.kind == OptionKind::Joined) {
                        joined.push_back(form.name + token);
                    } else if(form.kind == OptionKind::CommaJoined) {
                        joined.push_back(std::format("{}{}[,{}...]", form.name, token, token));
                    }
                    break;
                case NameForm::Role::OnlyEquals:
                    joined.push_back(form.name + token);
                    [[fallthrough]];
                case NameForm::Role::Equals: equals_forms.emplace_back(form, index); break;
            }
        }
        // The first name writes the option: after '=' when it has an '=' form, which reads
        // back any value.
        const auto& first = forms.front();
        arg.write_name = first.role == NameForm::Role::Own
                             ? first.name
                             : first.name.substr(0, first.name.size() - 1);
        if(first.role != NameForm::Role::Own ||
           (forms.size() > 1 && forms[1].role != NameForm::Role::Own &&
            forms[1].name == first.name + "=")) {
            arg.form = Form::Equals;
        } else if(first.kind == OptionKind::Joined || first.kind == OptionKind::CommaJoined) {
            arg.form = Form::Joined;
        }

        switch(arg.shape) {
            case Shape::Flag:
            case Shape::Count:
            case Shape::Alias: arg.usage = join(own, "|"); return;
            case Shape::Tuple:
                arg.usage = std::format("{} {}", join(own, "|"), numbered(token, arg.tuple_size));
                return;
            case Shape::Value: break;
        }
        std::vector<std::string> usages;
        if(!separate.empty()) {
            usages.push_back(std::format("{} {}", join(separate, "|"), token));
        }
        usages.insert(usages.end(), joined.begin(), joined.end());
        arg.usage = join(usages, "|");
    }

    void add_arg(Arg arg) {
        const auto index = static_cast<std::uint32_t>(command.args.size());
        auto& added = command.args.emplace_back(std::move(arg));
        const auto token = value_token(added.spec, *added.value_type);
        if(added.spec.rest) {
            command.rest = index;
            added.usage = std::format("-- {}...", token);
            return;
        }
        if(!added.spec.positional) {
            name(index);
            return;
        }
        command.positionals.push_back(index);
        const auto num = added.list ? added.spec.arg_num : ArgNum(1);
        added.usage = !added.list          ? token
                      : num.min == num.max ? numbered(token, num.min)
                                           : std::format("{}...", token);
        auto& count = command.positional_count;
        count.min += num.min;
        count.max = num.max == ArgNum::unbounded || count.max == ArgNum::unbounded
                        ? ArgNum::unbounded
                        : count.max + num.max;
    }

    void add_subcommands(const FieldUse& use,
                         std::uint32_t group,
                         const std::vector<std::string_view>& keys,
                         const std::string& path) {
        const auto& variant = static_cast<const meta::variant_type_info&>(*use.type);
        Subcommands subcommands{.keys = keys,
                                .path = path,
                                .optional = use.optional,
                                .group = group};
        for(std::size_t i = 0; i < variant.alternatives.size(); ++i) {
            const auto& alternative = variant.alternatives[i]();
            auto word = subcommand_name(variant, i);
            subcommands.hidden.push_back(
                std::ranges::find(use.spec.hidden_subcommands.names(), word) !=
                use.spec.hidden_subcommands.names().end());
            subcommands.names.push_back(std::move(word));
            subcommands.descriptions.push_back(
                static_cast<const meta::struct_type_info&>(alternative).description);
            subcommands.commands.push_back(build_command(alternative));
        }
        command.subcommands = std::move(subcommands);
    }

    void add_struct(const meta::struct_type_info& type,
                    std::uint32_t group,
                    const std::vector<std::string_view>& keys,
                    const std::string& path) {
        for(const auto& field: type.fields) {
            auto field_keys = keys;
            field_keys.push_back(field.name);
            const auto field_path = path.empty() ? std::string(field.declared_name)
                                                 : std::format("{}.{}", path, field.declared_name);
            const auto use = use_of(field);
            switch(use.kind) {
                case FieldUse::Kind::Group: {
                    const auto inner = static_cast<std::uint32_t>(command.groups.size());
                    command.groups.push_back(Group{.parent = group,
                                                   .optional = use.optional,
                                                   .keys = field_keys,
                                                   .description = field.description});
                    add_struct(static_cast<const meta::struct_type_info&>(*use.type),
                               inner,
                               field_keys,
                               field_path);
                    break;
                }
                case FieldUse::Kind::Subcommands:
                    add_subcommands(use, group, field_keys, field_path);
                    break;
                case FieldUse::Kind::Arg: {
                    Arg arg{use};
                    arg.field = &field;
                    arg.group = group;
                    arg.keys = std::move(field_keys);
                    arg.path = field_path;
                    arg.help = field.description;
                    add_arg(std::move(arg));
                    break;
                }
            }
        }
    }

public:
    explicit Builder(Command& command) : command(command) {}

    void build(const meta::struct_type_info& type) {
        command.type = &type;
        // The unknown option, then the input option, ids 1 and 2.
        command.options.push_back(Option::unknown(1));
        command.options.push_back(Option::input(2));
        // Ids start at 1; the unknown and input options read into no field.
        command.arg_of = {no_arg, no_arg, no_arg};
        add_struct(type, no_group, {}, "");
        name_equals_forms();
        // "--" ends the options: what follows is positional, or the rest when a field takes it.
        command.parse_options.dash_dash_parsing = true;
        command.parse_options.dash_dash_packing = command.rest.has_value();
        // "-" and "--" mark an option even in a command that names none, so that one it does
        // not know is not taken for a positional argument.
        std::set<std::string_view> prefixes = {"-", "--"};
        for(const auto& option: command.options) {
            prefixes.insert(option.prefixes.begin(), option.prefixes.end());
        }
        command.table = std::make_unique<OptTable>(command.options,
                                                   false,
                                                   std::vector(prefixes.begin(), prefixes.end()));
        command.table->search_includes_input = true;
    }
};

}  // namespace

std::unique_ptr<Command> build_command(const meta::type_info& type) {
    auto command = std::make_unique<Command>();
    Builder(*command).build(static_cast<const meta::struct_type_info&>(type));
    return command;
}

const Node* find_node(const Node& root, std::span<const std::string_view> keys) {
    const Node* node = &root;
    for(auto key: keys) {
        if(node->kind != Node::Kind::Object) {
            return nullptr;
        }
        const auto found = std::ranges::find(node->keys, key);
        if(found == node->keys.end()) {
            return nullptr;
        }
        node = &node->items[found - node->keys.begin()];
    }
    return node;
}

Node& make_node(Node& root, std::span<const std::string_view> keys, Node::Kind kind) {
    Node* node = &root;
    for(std::size_t i = 0; i < keys.size(); ++i) {
        const auto found = std::ranges::find(node->keys, keys[i]);
        if(found != node->keys.end()) {
            node = &node->items[found - node->keys.begin()];
            continue;
        }
        node->keys.push_back(keys[i]);
        node = &node->items.emplace_back(
            Node{.kind = i + 1 == keys.size() ? kind : Node::Kind::Object});
    }
    return *node;
}

const Command& command_of(const meta::type_info& type) {
    static std::mutex mutex;
    static std::map<const meta::type_info*, std::unique_ptr<Command>> commands;
    const std::lock_guard lock(mutex);
    auto& command = commands[&type];
    if(!command) {
        command = build_command(type);
    }
    return *command;
}

}  // namespace kotatsu::cli::detail
