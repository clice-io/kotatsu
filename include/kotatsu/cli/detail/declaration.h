#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kotatsu/cli/spec.h"
#include "kotatsu/cli/table/arg.h"
#include "kotatsu/support/config.h"
#include "kotatsu/support/naming.h"
#include "kotatsu/codec/type_info.h"

// What a struct of options declares, read from its type_info: the shape of each field and the
// names each option registers. A command is built from it at run time, and checked against it
// at compile time, by the same rules.

namespace kotatsu::cli::detail {

/// How an argument takes its values.
enum class Shape : std::uint8_t {
    /// A bool: given or not.
    Flag,
    /// An integer counting how often it is given.
    Count,
    /// One value an occurrence.
    Value,
    /// A tuple's elements an occurrence, one value each.
    Tuple,
    /// A cli::Alias: what it stands for, read in its place.
    Alias,
};

constexpr bool has_style(KVStyle style, KVStyle expected) {
    return (std::to_underlying(style) & std::to_underlying(expected)) != 0;
}

/// Whether a named value option of `style` spelled `name` takes its value joined to the name:
/// always for Joined, never for Separate, and for JoinedOrSeparate when the name ends in '='
/// or ':' (`--name=`, `/name:`).
constexpr bool joined_name(KVStyle style, std::string_view name) {
    if(!has_style(style, KVStyle::Joined)) {
        return false;
    }
    return !has_style(style, KVStyle::Separate) || name.ends_with('=') || name.ends_with(':');
}

/// `text` in kebab case: `ServeOptions` is "serve-options", `log_level` "log-level".
constexpr std::string kebab(std::string_view text) {
    auto name = naming::normalize_to_lower_snake(text);
    std::ranges::replace(name, '_', '-');
    return name;
}

/// What a field of options is on a command line.
struct FieldUse {
    enum class Kind : std::uint8_t {
        /// An option, a positional argument, or the arguments after "--".
        Arg,
        /// A struct of options.
        Group,
        /// A variant of structs, one a subcommand.
        Subcommands,
        /// A cli::exclusive variant of structs, groups of the command's arguments.
        Exclusive,
        /// A cli::Commands, subcommands files add.
        Commands,
    };

    Kind kind = Kind::Arg;
    ArgSpec spec = {};
    /// Whether the argv must give it: its presence is required (`presence = required`; a field
    /// may be left out otherwise, under cli::format). A positional argument of one value must be
    /// given too, unless it is a std::optional.
    bool required = false;
    /// Whether the field is a std::optional, the type below it `type`.
    bool optional = false;
    const codec::type_info* type = nullptr;
    /// For an argument: how it takes values, whether it collects them, how many a tuple
    /// takes, and the type of one value (a list's element).
    Shape shape = Shape::Value;
    bool list = false;
    std::uint32_t tuple_size = 0;
    const codec::type_info* value_type = nullptr;
};

constexpr FieldUse use_of(const codec::field_info& field) {
    FieldUse use;
    if(field.has_extension<ArgSpec>()) {
        use.spec = *field.extension<ArgSpec>();
    }
    use.required = field.presence == codec::presence_kind::required;
    use.type = &field.type();
    if(use.type->kind == meta::type_kind::optional) {
        use.optional = true;
        use.type = &static_cast<const codec::optional_type_info&>(*use.type).inner();
    }
    use.value_type = use.type;
    if(use.spec.commands.given) {
        use.kind = FieldUse::Kind::Commands;
        return use;
    }
    if(!use.spec.forward.empty()) {
        use.shape = Shape::Alias;
    } else {
        switch(use.type->kind) {
            case meta::type_kind::structure: use.kind = FieldUse::Kind::Group; return use;
            case meta::type_kind::variant:
                use.kind =
                    use.spec.exclusive ? FieldUse::Kind::Exclusive : FieldUse::Kind::Subcommands;
                return use;
            case meta::type_kind::array:
            case meta::type_kind::set:
                use.list = true;
                use.value_type = &static_cast<const codec::array_type_info&>(*use.type).element();
                break;
            default: break;
        }
        if(use.value_type->kind == meta::type_kind::tuple) {
            use.shape = Shape::Tuple;
            use.tuple_size = static_cast<std::uint32_t>(
                static_cast<const codec::tuple_type_info&>(*use.value_type).elements.size());
        } else if(use.value_type->kind == meta::type_kind::boolean && !use.list &&
                  !use.spec.positional && !use.spec.rest) {
            use.shape = Shape::Flag;
        } else if(use.spec.count) {
            use.shape = Shape::Count;
        }
    }
    use.required = use.required || (use.spec.positional && !use.list && !use.optional);
    return use;
}

/// One spelling an option registers: a name of its own, or the '=' form of one, which a field
/// naming it so keeps unless it is the only form its option takes.
struct NameForm {
    enum class Role : std::uint8_t {
        Own,
        Equals,
        OnlyEquals,
    };

    std::string name;
    OptionKind kind = OptionKind::Flag;
    Role role = Role::Own;
};

/// The names an option is given by: its own, else its declared name with '_' spelled '-',
/// after "-" when one letter long and "--" otherwise.
constexpr std::vector<std::string> names_of(const ArgSpec& spec, std::string_view declared) {
    std::vector<std::string> names;
    for(auto name: spec.names) {
        names.emplace_back(name);
    }
    if(names.empty()) {
        auto name = std::string(declared.size() == 1 ? "-" : "--") + std::string(declared);
        std::ranges::replace(name, '_', '-');
        names.push_back(std::move(name));
    }
    return names;
}

/// The spellings a named option registers, in the order of its names.
constexpr std::vector<NameForm> name_forms(const FieldUse& use, std::string_view declared) {
    std::vector<NameForm> forms;
    const auto names = names_of(use.spec, declared);
    switch(use.shape) {
        case Shape::Flag:
        case Shape::Count:
        case Shape::Alias:
            for(const auto& name: names) {
                forms.push_back(NameForm{.name = name, .kind = OptionKind::Flag});
            }
            return forms;
        case Shape::Tuple:
            for(const auto& name: names) {
                forms.push_back(NameForm{.name = name, .kind = OptionKind::MultiArg});
            }
            return forms;
        case Shape::Value: break;
    }
    if(use.spec.comma) {
        for(auto name: names) {
            if(!name.ends_with(',')) {
                name.push_back(',');
            }
            forms.push_back(NameForm{.name = std::move(name), .kind = OptionKind::CommaJoined});
        }
        return forms;
    }
    const auto style = use.spec.style;
    const bool generated = use.spec.names.empty();
    for(const auto& name: names) {
        // A generated name of a Joined option takes its value after '='.
        const bool joins =
            generated ? !has_style(style, KVStyle::Separate) : joined_name(style, name);
        if(joins && !generated) {
            forms.push_back(NameForm{.name = name, .kind = OptionKind::Joined});
            continue;
        }
        if(!joins) {
            forms.push_back(NameForm{.name = name, .kind = OptionKind::Separate});
        }
        if(has_style(style, KVStyle::Joined)) {
            forms.push_back(
                NameForm{.name = name + "=",
                         .kind = OptionKind::Joined,
                         .role = joins ? NameForm::Role::OnlyEquals : NameForm::Role::Equals});
        }
    }
    return forms;
}

/// The word that names alternative `index` of a variant of subcommands: its tag name, else its
/// type's name in kebab case.
constexpr std::string subcommand_name(const codec::variant_type_info& variant, std::size_t index) {
    if(variant.tagging != codec::tag_mode::none && index < variant.alt_names.size()) {
        return std::string(variant.alt_names[index]);
    }
    return kebab(variant.alternatives[index]().type_name);
}

/// Checks what a struct of options declares, and the subcommands it holds, failing the
/// constant evaluation it runs in, when there, at the first mistake.
class DeclarationCheck {
    std::vector<std::string> names;
    std::vector<NameForm> equals_forms;
    std::vector<std::string> alias_names;
    std::vector<std::string_view> forwards;
    bool rest = false;
    bool subcommands = false;
    /// Whether the fields being read are of an alternative of an exclusive field, and whether
    /// that alternative has a positional argument.
    bool in_alternative = false;
    bool alternative_positional = false;

    constexpr void claim_subcommands() {
        if(subcommands) {
            KOTATSU_THROW("cli: a command has one field of subcommands");
        }
        subcommands = true;
    }

    constexpr void check_name(std::string_view name) {
        const bool prefixed = (name.starts_with("--") && name.size() > 2) ||
                              ((name.starts_with('-') || name.starts_with('/')) && name.size() > 1);
        if(!prefixed) {
            KOTATSU_THROW("cli: an option name starts with '-', '--' or '/' and goes on");
        }
    }

    constexpr void check_subcommands(const codec::variant_type_info& variant, const FieldUse& use) {
        claim_subcommands();
        if(variant.tagging != codec::tag_mode::none &&
           variant.tagging != codec::tag_mode::external) {
            KOTATSU_THROW("cli: subcommands are named by their tag names alone");
        }
        std::vector<std::string> words;
        for(std::size_t i = 0; i < variant.alternatives.size(); ++i) {
            const auto& alternative = variant.alternatives[i]();
            if(alternative.kind != meta::type_kind::structure) {
                KOTATSU_THROW("cli: a subcommand is a struct");
            }
            words.push_back(subcommand_name(variant, i));
            DeclarationCheck inner;
            inner.check(static_cast<const codec::struct_type_info&>(alternative));
        }
        for(auto hidden: use.spec.hidden_sub) {
            if(std::ranges::find(words, hidden) == words.end()) {
                KOTATSU_THROW("cli: a hidden subcommand is one of the field's");
            }
        }
        std::ranges::sort(words);
        if(std::ranges::adjacent_find(words) != words.end()) {
            KOTATSU_THROW("cli: two subcommands are named the same");
        }
    }

    /// An exclusive field's alternatives join the command's arguments; one holds no
    /// subcommands and no exclusive field of its own, and one alternative alone has positional
    /// arguments, which the argv then gives to it.
    constexpr void check_exclusive(const codec::variant_type_info& variant) {
        if(variant.tagging != codec::tag_mode::none) {
            KOTATSU_THROW("cli: an exclusive field is untagged");
        }
        std::size_t positional = 0;
        in_alternative = true;
        for(const auto& alternative: variant.alternatives) {
            if(alternative().kind != meta::type_kind::structure) {
                KOTATSU_THROW("cli: an alternative of an exclusive field is a struct");
            }
            alternative_positional = false;
            add_struct(static_cast<const codec::struct_type_info&>(alternative()));
            positional += alternative_positional ? 1 : 0;
        }
        in_alternative = false;
        if(positional > 1) {
            KOTATSU_THROW("cli: one alternative of an exclusive field has positional arguments");
        }
    }

    constexpr void check_arg(const codec::field_info& field, const FieldUse& use) {
        if(use.spec.rest) {
            if(!use.list) {
                KOTATSU_THROW("cli: the rest is a list");
            }
            if(rest) {
                KOTATSU_THROW("cli: one field takes the rest");
            }
            rest = true;
            return;
        }
        if(use.spec.positional) {
            alternative_positional = in_alternative;
            return;
        }
        if(use.spec.comma && !use.list) {
            KOTATSU_THROW("cli: a comma option is a list");
        }
        for(auto& form: name_forms(use, field.declared_name)) {
            check_name(form.name);
            if(use.shape == Shape::Alias) {
                alias_names.push_back(form.name);
            }
            if(form.role == NameForm::Role::Own) {
                names.push_back(std::move(form.name));
            } else {
                equals_forms.push_back(std::move(form));
            }
        }
        if(use.shape == Shape::Alias) {
            forwards.insert(forwards.end(), use.spec.forward.begin(), use.spec.forward.end());
        }
    }

    constexpr void add_struct(const codec::struct_type_info& type) {
        for(const auto& field: type.fields) {
            const auto use = use_of(field);
            if(use.spec.exclusive && use.kind != FieldUse::Kind::Exclusive) {
                KOTATSU_THROW("cli: an exclusive field is a std::variant of structs");
            }
            if(in_alternative && use.kind != FieldUse::Kind::Group &&
               use.kind != FieldUse::Kind::Arg) {
                KOTATSU_THROW(
                    "cli: an alternative of an exclusive field has no subcommands, "
                    "and no exclusive field");
            }
            switch(use.kind) {
                case FieldUse::Kind::Group:
                    add_struct(static_cast<const codec::struct_type_info&>(*use.type));
                    break;
                case FieldUse::Kind::Subcommands:
                    check_subcommands(static_cast<const codec::variant_type_info&>(*use.type), use);
                    break;
                // The subcommands of a set are checked where their files add them.
                case FieldUse::Kind::Commands: claim_subcommands(); break;
                case FieldUse::Kind::Exclusive:
                    check_exclusive(static_cast<const codec::variant_type_info&>(*use.type));
                    break;
                case FieldUse::Kind::Arg: check_arg(field, use); break;
            }
        }
    }

public:
    constexpr void check(const codec::struct_type_info& type) {
        add_struct(type);
        // An '=' form yields to an option named so, unless it is the only form of its own.
        for(auto& form: equals_forms) {
            const bool named = std::ranges::find(names, form.name) != names.end();
            if(named && form.role == NameForm::Role::OnlyEquals) {
                KOTATSU_THROW("cli: two options are named the same");
            }
            if(!named) {
                names.push_back(std::move(form.name));
            }
        }
        for(auto token: forwards) {
            if(std::ranges::find(alias_names, token) != alias_names.end()) {
                KOTATSU_THROW("cli: an alias stands for another alias");
            }
        }
        std::ranges::sort(names);
        if(std::ranges::adjacent_find(names) != names.end()) {
            KOTATSU_THROW("cli: two options are named the same");
        }
    }
};

/// Whether `type` declares options cli takes; a mistake in them fails the constant evaluation
/// this runs in, at the check that finds it.
constexpr bool check_declaration(const codec::type_info& type) {
    if(type.kind != meta::type_kind::structure) {
        KOTATSU_THROW("cli: options are declared in a struct");
    }
    DeclarationCheck check;
    check.check(static_cast<const codec::struct_type_info&>(type));
    return true;
}

}  // namespace kotatsu::cli::detail
