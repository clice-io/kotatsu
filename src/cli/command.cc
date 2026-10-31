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
#include "kotatsu/support/config.h"
#include "kotatsu/support/naming.h"

namespace kotatsu::cli::detail {

namespace {

bool has_style(KVStyle style, KVStyle expected) {
    return (std::to_underlying(style) & std::to_underlying(expected)) != 0;
}

/// Whether a named value option of `style` spelled `name` takes its value joined to the name:
/// always for Joined, never for Separate, and for JoinedOrSeparate when the name ends in '='
/// or ':' (`--name=`, `/name:`).
bool joined_name(KVStyle style, std::string_view name) {
    if(!has_style(style, KVStyle::Joined)) {
        return false;
    }
    return !has_style(style, KVStyle::Separate) || name.ends_with('=') || name.ends_with(':');
}

/// The prefixes an option named `name` accepts: "--name" takes "--", "-n" takes "-", and
/// "/name" takes "/" or "-".
std::span<const std::string_view> prefixes_of(std::string_view name) {
    if(name.starts_with("--") && name.size() > 2) {
        return pfx_double;
    }
    if(name.starts_with("-") && name.size() > 1) {
        return pfx_dash;
    }
    if(name.starts_with("/") && name.size() > 1) {
        return pfx_slash_dash;
    }
    KOTATSU_THROW(std::invalid_argument(
        std::format("cli: option name '{}' starts with '-', '--' or '/' and goes on", name)));
}

/// `text` in kebab case: `ServeOptions` is "serve-options", `log_level` "log-level".
std::string kebab(std::string_view text) {
    auto name = naming::normalize_to_lower_snake(text);
    std::ranges::replace(name, '_', '-');
    return name;
}

const meta::type_info& unwrap_optional(const meta::type_info& type, bool& optional) {
    if(type.kind == meta::type_kind::optional) {
        optional = true;
        return static_cast<const meta::optional_type_info&>(type).inner();
    }
    return type;
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

class Builder {
    /// The '=' form of a name, and whether it is the only form its argument takes.
    struct EqualsForm {
        std::string name;
        std::uint32_t arg;
        bool only;
    };

    Command& command;
    std::vector<EqualsForm> equals_forms;

    std::uint32_t add_option(OptionKind kind, std::string_view name, std::uint32_t arg) {
        const auto id = static_cast<std::uint32_t>(command.options.size() + 1);
        const auto& spelling = command.spellings.emplace_back(name);
        command.options.push_back(
            Option::unaliased_one(prefixes_of(spelling), spelling, id, kind, 0, "", ""));
        command.arg_of.push_back(arg);
        return id;
    }

    /// The names `arg` is given by: its own, else its declared name's.
    static std::vector<std::string> names_of(const Arg& arg) {
        std::vector<std::string> names;
        for(auto name: arg.spec.names.names()) {
            names.emplace_back(name);
        }
        if(names.empty()) {
            const auto declared = arg.field->declared_name;
            auto name = std::string(declared.size() == 1 ? "-" : "--") + std::string(declared);
            std::ranges::replace(name, '_', '-');
            names.push_back(std::move(name));
        }
        return names;
    }

    /// The argument the option named `name` reads into, or no_arg when none is named so.
    std::uint32_t arg_named(std::string_view name) const {
        const auto found = std::ranges::find(command.spellings, name);
        // Spelling i names the option of id i + 3, after the unknown and input options.
        return found == command.spellings.end()
                   ? no_arg
                   : command.arg_of[found - command.spellings.begin() + 3];
    }

    /// Names the '=' forms no field names itself, then checks that no two options share a name.
    void name_equals_forms() {
        for(const auto& form: equals_forms) {
            auto& arg = command.args[form.arg];
            if(arg_named(form.name) == no_arg) {
                add_option(OptionKind::Joined, form.name, form.arg);
            } else if(form.only) {
                KOTATSU_THROW(
                    std::invalid_argument(std::format("cli: two options are named {}", form.name)));
            } else if(arg.form == Form::Equals && arg.write_name + "=" == form.name) {
                arg.form = Form::Separate;
            }
        }
        // An alias stands for options, not for another alias, which might stand for it.
        for(const auto& arg: command.args) {
            if(arg.shape != Shape::Alias) {
                continue;
            }
            for(auto token: arg.spec.forward.names()) {
                const auto target = arg_named(token);
                if(target != no_arg && command.args[target].shape == Shape::Alias) {
                    KOTATSU_THROW(std::invalid_argument(
                        std::format("cli: alias {} stands for another alias", arg.usage)));
                }
            }
        }
        std::vector<std::string_view> names(command.spellings.begin(), command.spellings.end());
        std::ranges::sort(names);
        if(const auto twice = std::ranges::adjacent_find(names); twice != names.end()) {
            KOTATSU_THROW(
                std::invalid_argument(std::format("cli: two options are named {}", *twice)));
        }
    }

    /// Registers named argument `index` under its names, and says how a usage writes it.
    void name(std::uint32_t index, const meta::type_info& value_type) {
        auto& arg = command.args[index];
        const auto names = names_of(arg);
        const bool generated = arg.spec.names.names().empty();
        switch(arg.shape) {
            case Shape::Flag:
            case Shape::Count:
            case Shape::Alias: {
                for(const auto& name: names) {
                    add_option(OptionKind::Flag, name, index);
                }
                arg.usage = join(names, "|");
                arg.write_name = names.front();
                return;
            }
            case Shape::Tuple: {
                for(const auto& name: names) {
                    add_option(OptionKind::MultiArg, name, index);
                    command.options.back().num_args = static_cast<unsigned char>(arg.tuple_size);
                }
                arg.usage =
                    std::format("{} {}",
                                join(names, "|"),
                                numbered(value_token(arg.spec, value_type), arg.tuple_size));
                arg.write_name = names.front();
                return;
            }
            case Shape::Value: break;
        }
        const auto token = value_token(arg.spec, value_type);
        if(arg.spec.comma) {
            if(!arg.list) {
                KOTATSU_THROW(std::invalid_argument("cli: a comma option is a list"));
            }
            std::vector<std::string> forms;
            for(auto name: names) {
                if(!name.ends_with(',')) {
                    name.push_back(',');
                }
                add_option(OptionKind::CommaJoined, name, index);
                forms.push_back(std::format("{}{}[,{}...]", name, token, token));
                if(arg.write_name.empty()) {
                    arg.write_name = name;
                }
            }
            arg.usage = join(forms, "|");
            return;
        }
        const auto style = arg.spec.style;
        std::vector<std::string> separate;
        std::vector<std::string> joined;
        for(const auto& name: names) {
            // A generated name of a Joined option takes its value after '='.
            const bool joins =
                generated ? !has_style(style, KVStyle::Separate) : joined_name(style, name);
            const bool first = arg.write_name.empty();
            if(first) {
                arg.write_name = name;
            }
            if(joins && !generated) {
                add_option(OptionKind::Joined, name, index);
                joined.push_back(name + token);
                if(first) {
                    arg.form = Form::Joined;
                }
                continue;
            }
            if(!joins) {
                add_option(OptionKind::Separate, name, index);
                separate.push_back(name);
            }
            // The '=' form a name takes its value after when it may take it joined; a usage
            // leaves it out unless it is the only form. It is named once every field has named
            // its own, so that one naming it so, declared before or after, keeps it.
            if(has_style(style, KVStyle::Joined)) {
                equals_forms.push_back(EqualsForm{.name = name + "=", .arg = index, .only = joins});
                if(joins) {
                    joined.push_back(name + "=" + token);
                }
                if(first) {
                    arg.form = Form::Equals;
                }
            }
        }
        std::vector<std::string> forms;
        if(!separate.empty()) {
            forms.push_back(std::format("{} {}", join(separate, "|"), token));
        }
        forms.insert(forms.end(), joined.begin(), joined.end());
        arg.usage = join(forms, "|");
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
            bool optional = false;
            const auto& value_type = unwrap_optional(field.type(), optional);
            const auto* annotated = field.extension<ArgSpec>();
            const ArgSpec spec = annotated ? *annotated : ArgSpec{};

            if(!spec.forward.names().empty()) {
                add_arg(Arg{.field = &field,
                            .spec = spec,
                            .shape = Shape::Alias,
                            .group = group,
                            .keys = field_keys,
                            .path = field_path,
                            .help = field.description},
                        value_type);
                continue;
            }
            switch(value_type.kind) {
                case meta::type_kind::structure: {
                    const auto inner = static_cast<std::uint32_t>(command.groups.size());
                    command.groups.push_back(Group{.parent = group,
                                                   .optional = optional,
                                                   .keys = field_keys,
                                                   .description = field.description});
                    add_struct(static_cast<const meta::struct_type_info&>(value_type),
                               inner,
                               field_keys,
                               field_path);
                    continue;
                }
                case meta::type_kind::variant: {
                    add_subcommands(static_cast<const meta::variant_type_info&>(value_type),
                                    optional,
                                    group,
                                    field_keys,
                                    field_path);
                    continue;
                }
                default: break;
            }

            Arg arg{.field = &field,
                    .spec = spec,
                    .shape = Shape::Value,
                    .optional_type = optional,
                    .group = group,
                    .keys = field_keys,
                    .path = field_path,
                    .help = field.description};
            const meta::type_info* element = &value_type;
            if(value_type.kind == meta::type_kind::array ||
               value_type.kind == meta::type_kind::set) {
                arg.list = true;
                element = &static_cast<const meta::array_type_info&>(value_type).element();
            }
            if(element->kind == meta::type_kind::tuple) {
                arg.shape = Shape::Tuple;
                arg.tuple_size = static_cast<std::uint32_t>(
                    static_cast<const meta::tuple_type_info&>(*element).elements.size());
            } else if(element->kind == meta::type_kind::boolean && !arg.list && !spec.positional) {
                arg.shape = Shape::Flag;
            } else if(spec.count) {
                arg.shape = Shape::Count;
            }
            add_arg(std::move(arg), *element);
        }
    }

    void add_arg(Arg arg, const meta::type_info& value_type) {
        const auto index = static_cast<std::uint32_t>(command.args.size());
        command.args.push_back(std::move(arg));
        auto& added = command.args.back();
        if(added.spec.rest) {
            if(command.rest) {
                KOTATSU_THROW(std::invalid_argument("cli: one field takes the rest"));
            }
            command.rest = index;
            added.list = true;
            added.usage = std::format("-- {}...", value_token(added.spec, value_type));
            return;
        }
        if(added.spec.positional) {
            command.positionals.push_back(index);
            const auto token = value_token(added.spec, value_type);
            if(!added.list) {
                added.usage = token;
                ++command.positional_count.min;
                command.positional_count.max = command.positional_count.max == ArgNum::unbounded
                                                   ? ArgNum::unbounded
                                                   : command.positional_count.max + 1;
            } else {
                const auto num = added.spec.arg_num;
                added.usage =
                    num.min == num.max ? numbered(token, num.min) : std::format("{}...", token);
                command.positional_count.min += num.min;
                command.positional_count.max =
                    num.max == ArgNum::unbounded ||
                            command.positional_count.max == ArgNum::unbounded
                        ? ArgNum::unbounded
                        : command.positional_count.max + num.max;
            }
            return;
        }
        name(index, value_type);
    }

    void add_subcommands(const meta::variant_type_info& variant,
                         bool optional,
                         std::uint32_t group,
                         const std::vector<std::string_view>& keys,
                         const std::string& path) {
        if(command.subcommands) {
            KOTATSU_THROW(std::invalid_argument("cli: a command has one field of subcommands"));
        }
        if(variant.tagging != meta::tag_mode::none && variant.tagging != meta::tag_mode::external) {
            KOTATSU_THROW(
                std::invalid_argument("cli: subcommands are named by their tag names alone"));
        }
        Subcommands subcommands{.keys = keys, .path = path, .optional = optional, .group = group};
        for(std::size_t i = 0; i < variant.alternatives.size(); ++i) {
            const auto& alternative = variant.alternatives[i]();
            if(alternative.kind != meta::type_kind::structure) {
                KOTATSU_THROW(std::invalid_argument("cli: a subcommand is a struct"));
            }
            subcommands.names.push_back(i < variant.alt_names.size() &&
                                                variant.tagging != meta::tag_mode::none
                                            ? std::string(variant.alt_names[i])
                                            : kebab(alternative.type_name));
            subcommands.descriptions.push_back(
                static_cast<const meta::struct_type_info&>(alternative).description);
            subcommands.commands.push_back(build_command(alternative));
        }
        command.subcommands = std::move(subcommands);
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
    if(type.kind != meta::type_kind::structure) {
        KOTATSU_THROW(std::invalid_argument("cli: options are declared in a struct"));
    }
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
