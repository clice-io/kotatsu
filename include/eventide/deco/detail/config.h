#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

namespace deco::config {

struct PositionStyle {
    bool enabled = true;
    bool show_label = true;
    bool show_source_line = true;
    std::size_t max_source_width = 96;
    char pointer = '^';
    char underline = '~';
};

struct UsageStyle {
    bool group_by_category = true;
    std::size_t help_column = 32;
    std::string options_heading = "Options:";
    std::string group_prefix = "Group ";
    std::string exclusive_suffix = ", exclusive with other groups";
    std::string default_help = "no description provided";
};

struct SubCommandStyle {
    bool show_overview = true;
    bool show_usage_line = true;
    bool show_description = true;
    bool align_description = true;
    bool show_command_alias = true;
    std::string heading = "Subcommands:";
};

struct TextStyle {
    PositionStyle diagnostic{};
    UsageStyle usage{};
    SubCommandStyle subcommand{};
};

struct CompatibleRendererConfig {
    PositionStyle diagnostic{};
    UsageStyle usage{};
    SubCommandStyle subcommand{};
};

struct ModernRendererConfig {
    PositionStyle diagnostic{};
    UsageStyle usage{};
    SubCommandStyle subcommand{};

    ModernRendererConfig() {
        usage.options_heading = "Options";
        subcommand.heading = "Commands";
    }
};

struct BuiltInRenderConfig {
    CompatibleRendererConfig compatible{};
    ModernRendererConfig modern{};
};

struct EnumMetaVarConfig {
    bool enabled = true;
    std::size_t max_items = 6;
    std::string separator = "|";
    std::string overflow_suffix = "|...";
};

struct Config {
    EnumMetaVarConfig enum_meta_var{};
    BuiltInRenderConfig render{};
};

struct ConfigOverride {
    std::optional<EnumMetaVarConfig> enum_meta_var;
    std::optional<BuiltInRenderConfig> render;
};

inline auto merge(Config base, const ConfigOverride& override_config) -> Config {
    if(override_config.enum_meta_var.has_value()) {
        base.enum_meta_var = *override_config.enum_meta_var;
    }
    if(override_config.render.has_value()) {
        base.render = *override_config.render;
    }
    return base;
}

auto get() -> const Config&;
void set(Config config);
void set_render(BuiltInRenderConfig render);
void set_enum_meta_var(EnumMetaVarConfig config);

}  // namespace deco::config
