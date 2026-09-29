#pragma once
#include <algorithm>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "config.h"
#include "decl.h"
#include "text.h"
#include "ty.h"
#include "kota/support/spelling.h"

/*
 * Describes declared options in text: the forms an option is written in (`-o|--output <FILE>`),
 * and with its help, the line of a usage listing.
 */

namespace kota::deco::desc {
namespace detail {

constexpr std::string_view default_help_text = "not provided";

constexpr inline bool has_help_text(std::string_view help_text) {
    return !help_text.empty() && help_text != default_help_text;
}

inline std::string category_desc(const decl::Category& category) {
    if(!category.name.empty() && !category.description.empty()) {
        return std::format("<{}> ({})", category.name, category.description);
    }
    if(!category.name.empty()) {
        return std::format("<{}>", category.name);
    }
    if(!category.description.empty()) {
        return std::string(category.description);
    }
    return std::string("<unnamed category>");
}

inline std::string meta_var_token(std::string_view meta_var) {
    if(meta_var.empty()) {
        return "<value>";
    }
    if(meta_var.front() == '<' && meta_var.back() == '>') {
        return std::string(meta_var);
    }
    return std::format("<{}>", meta_var);
}

inline std::string enum_meta_var_token(const std::vector<std::string>& names,
                                       const config::EnumMetaVarConfig& cfg) {
    if(names.empty()) {
        return "<value>";
    }

    std::string body;
    const auto limit = std::min<std::size_t>(names.size(), cfg.max_items);
    for(std::size_t i = 0; i < limit; ++i) {
        if(i != 0) {
            body += cfg.separator;
        }
        body += names[i];
    }
    if(names.size() > limit) {
        body += cfg.overflow_suffix;
    }
    return std::format("<{}>", body);
}

inline auto active_config(const config::Config* override_config) -> const config::Config& {
    if(override_config != nullptr) {
        return *override_config;
    }
    return config::get();
}

template <typename ResultTy, typename = void>
struct inferred_enum_meta_var_type {
    using type = void;
};

template <typename ResultTy>
    requires std::is_enum_v<ty::base_ty<ResultTy>>
struct inferred_enum_meta_var_type<ResultTy, void> {
    using type = ty::base_ty<ResultTy>;
};

template <typename ResultTy>
struct inferred_enum_meta_var_type<ResultTy,
                                   std::void_t<std::ranges::range_value_t<ty::base_ty<ResultTy>>>> {
private:
    using base_result_ty = ty::base_ty<ResultTy>;
    using element_ty = std::remove_cvref_t<std::ranges::range_value_t<base_result_ty>>;

public:
    using type =
        std::conditional_t<trait::VectorResultType<base_result_ty> &&
                               std::ranges::range<base_result_ty> && std::is_enum_v<element_ty>,
                           element_ty,
                           void>;
};

template <typename ResultTy>
using inferred_enum_meta_var_type_t = typename inferred_enum_meta_var_type<ResultTy>::type;

template <typename ResultTy>
inline std::string inferred_meta_var_token(const decl::MetaVarField& meta_var,
                                           const config::Config& config) {
    using enum_ty = inferred_enum_meta_var_type_t<ResultTy>;
    if(meta_var.is_explicit()) {
        return meta_var_token(meta_var.value);
    }
    if constexpr(!std::is_void_v<enum_ty>) {
        if(config.enum_meta_var.enabled) {
            return enum_meta_var_token(kota::codec::spelling::enum_strings<enum_ty>(),
                                       config.enum_meta_var);
        }
    }
    return meta_var_token(meta_var.value);
}

inline std::string join_strings(const std::vector<std::string>& parts, std::string_view separator) {
    if(parts.empty()) {
        return "";
    }
    std::string joined = parts.front();
    for(std::size_t i = 1; i < parts.size(); ++i) {
        joined += separator;
        joined += parts[i];
    }
    return joined;
}

inline std::string placeholder_name(decl::DecoType deco_field_ty) {
    switch(deco_field_ty) {
        case decl::DecoType::Flag: return "--<flag>";
        case decl::DecoType::KV: return "--<option>";
        case decl::DecoType::CommaJoined: return "--<list-option>";
        case decl::DecoType::Multi: return "--<multi-option>";
        default: return "<option>";
    }
}

/// The names of named option `cfg`: its explicit names, else the one generated from its
/// field, else, for a field that is a placeholder, a placeholder of its kind.
template <typename CfgTy>
inline std::vector<std::string> named_aliases(const CfgTy& cfg, std::string_view fallback_name) {
    if(!cfg.names.empty()) {
        return std::vector<std::string>(cfg.names.begin(), cfg.names.end());
    }
    if(fallback_name.empty() || decl::is_alias_placeholder_name(fallback_name)) {
        return {placeholder_name(CfgTy::deco_field_ty)};
    }
    return {decl::detail::generated_option_name(fallback_name)};
}

inline std::string join_aliases(const std::vector<std::string>& aliases, bool help_mode) {
    return join_strings(aliases, help_mode ? ", " : "|");
}

/// The forms KV option `cfg` is written in, as the parser takes them: its names that take
/// their value separate together, `-o|--output <FILE>`, then each that takes it joined,
/// `--output=<FILE>`. A generated name takes its value joined after '='.
template <typename CfgTy>
inline std::string kv_usage_text(const CfgTy& cfg,
                                 bool help_mode,
                                 std::string_view fallback_name,
                                 std::string_view value_token) {
    std::vector<std::string> separate;
    std::vector<std::string> joined;
    if(cfg.names.empty()) {
        const auto name = named_aliases(cfg, fallback_name).front();
        if(decl::detail::has_kv_style(cfg.style, decl::KVStyle::Separate)) {
            separate.push_back(name);
        }
        if(decl::detail::has_kv_style(cfg.style, decl::KVStyle::Joined)) {
            joined.push_back(name + "=");
        }
    } else {
        for(const auto name: cfg.names) {
            (decl::detail::is_joined_kv_name(cfg.style, name) ? joined : separate)
                .emplace_back(name);
        }
    }

    std::vector<std::string> forms;
    if(!separate.empty()) {
        forms.push_back(std::format("{} {}", join_aliases(separate, help_mode), value_token));
    }
    for(const auto& name: joined) {
        forms.push_back(name + std::string(value_token));
    }
    return join_aliases(forms, help_mode);
}

inline std::string comma_joined_alias(std::string_view alias, std::string_view value_token) {
    return std::format("{},{}[,{}...]", alias, value_token, value_token);
}

inline std::string base_meta_name(std::string_view value_token) {
    if(value_token.size() >= 2 && value_token.front() == '<' && value_token.back() == '>') {
        return std::string(value_token.substr(1, value_token.size() - 2));
    }
    return std::string(value_token);
}

inline std::string repeated_meta_vars(std::string_view value_token, unsigned arg_num) {
    if(arg_num <= 1) {
        return std::string(value_token);
    }
    if(value_token.find('|') != std::string_view::npos) {
        std::vector<std::string> values(arg_num, std::string(value_token));
        return join_strings(values, " ");
    }

    const auto base_name = base_meta_name(value_token);
    std::vector<std::string> values;
    values.reserve(arg_num);
    for(unsigned i = 1; i <= arg_num; ++i) {
        values.push_back(std::format("<{}{}>", base_name, i));
    }
    return join_strings(values, " ");
}

template <typename CfgTy>
inline std::string usage_text(const CfgTy& cfg,
                              bool help_mode,
                              std::string_view fallback_name,
                              std::string_view value_token) {
    if constexpr(CfgTy::deco_field_ty == decl::DecoType::Input) {
        return std::string(value_token);
    } else if constexpr(CfgTy::deco_field_ty == decl::DecoType::TrailingInput) {
        return std::format("-- {}...", value_token);
    } else if constexpr(CfgTy::deco_field_ty == decl::DecoType::Flag) {
        return join_aliases(named_aliases(cfg, fallback_name), help_mode);
    } else if constexpr(CfgTy::deco_field_ty == decl::DecoType::KV) {
        return kv_usage_text(cfg, help_mode, fallback_name, value_token);
    } else if constexpr(CfgTy::deco_field_ty == decl::DecoType::CommaJoined) {
        const auto aliases = named_aliases(cfg, fallback_name);
        std::vector<std::string> forms;
        forms.reserve(aliases.size());
        for(const auto& alias: aliases) {
            forms.push_back(comma_joined_alias(alias, value_token));
        }
        return join_aliases(forms, help_mode);
    } else {
        static_assert(CfgTy::deco_field_ty == decl::DecoType::Multi);
        return std::format("{} {}",
                           join_aliases(named_aliases(cfg, fallback_name), help_mode),
                           repeated_meta_vars(value_token, cfg.arg_num));
    }
}

template <typename FieldTy, typename CfgTy>
inline std::string usage_text_for_field(const FieldTy&,
                                        const CfgTy& cfg,
                                        bool help_mode,
                                        std::string_view fallback_name,
                                        const config::Config& config) {
    if constexpr(ty::deco_option_like<FieldTy>) {
        using result_ty = typename ty::base_ty<FieldTy>::result_type;
        const auto value_token = inferred_meta_var_token<result_ty>(cfg.meta_var, config);
        if constexpr(CfgTy::deco_field_ty == decl::DecoType::Input) {
            if constexpr(!trait::ScalarResultType<result_ty> &&
                         trait::VectorResultType<result_ty>) {
                return std::format("{}...", value_token);
            } else {
                return value_token;
            }
        } else {
            return usage_text(cfg, help_mode, fallback_name, value_token);
        }
    } else {
        return usage_text(cfg, help_mode, fallback_name, meta_var_token(cfg.meta_var));
    }
}

}  // namespace detail

/// The forms `field` is written in; with `include_help`, its line in a usage listing, laid out
/// as the compatible renderer of `override_config` (or the global config) lays it out.
/// `fallback_name` names a field declared without names.
template <ty::is_deco_field_or_option T>
inline std::string from_deco_option(const T& field,
                                    bool include_help = false,
                                    std::string_view fallback_name = {},
                                    const config::Config* override_config = nullptr) {
    const auto cfg = ty::dyn_cast(field);
    const auto& config = detail::active_config(override_config);
    auto usage = detail::usage_text_for_field(field, cfg, include_help, fallback_name, config);
    if(!include_help) {
        return usage;
    }
    return cli::text::render_usage_entry(
        cli::text::UsageEntry{
            .usage = std::move(usage),
            .help = detail::has_help_text(cfg.help) ? std::string(cfg.help) : std::string{},
        },
        config.render.compatible.usage);
}

}  // namespace kota::deco::desc
