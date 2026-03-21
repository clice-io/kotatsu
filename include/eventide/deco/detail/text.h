#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace deco::cli::text {

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
    std::string_view options_heading = "Options:";
    std::string_view group_prefix = "Group ";
    std::string_view exclusive_suffix = ", exclusive with other groups";
    std::string_view default_help = "no description provided";
};

struct SubCommandStyle {
    bool show_overview = true;
    bool show_usage_line = true;
    bool show_description = true;
    bool align_description = true;
    bool show_command_alias = true;
    std::string_view heading = "Subcommands:";
};

struct TextStyle {
    PositionStyle diagnostic{};
    UsageStyle usage{};
    SubCommandStyle subcommand{};
};

struct Diagnostic {
    std::string message;
    std::span<const std::string> argv{};
    unsigned begin = 0;
    unsigned end = 0;
    bool positioned = false;
};

struct UsageEntry {
    std::string usage;
    std::string help;
};

struct UsageGroup {
    std::string title;
    bool exclusive = false;
    bool is_default = false;
    std::vector<UsageEntry> entries;
};

struct UsageDocument {
    std::string overview;
    std::vector<UsageGroup> groups;
};

struct SubCommandEntry {
    std::string name;
    std::string description;
    std::string command;
};

struct SubCommandDocument {
    std::string overview;
    std::string usage_line;
    bool has_usage_line = false;
    std::vector<SubCommandEntry> entries;
};

auto looks_like_rendered_diagnostic(std::string_view text) -> bool;

auto diagnostic_at(std::span<const std::string> argv,
                   unsigned begin,
                   unsigned end,
                   std::string message) -> Diagnostic;

auto diagnostic_message(std::string message) -> Diagnostic;

struct Renderer {
    using usage_renderer_t =
        std::function<std::string(const UsageDocument&, bool, const TextStyle&)>;
    using subcommand_renderer_t =
        std::function<std::string(const SubCommandDocument&, const TextStyle&)>;
    using diagnostic_renderer_t =
        std::function<std::string(const Diagnostic&, const TextStyle&)>;

    TextStyle style{};
    usage_renderer_t usage;
    subcommand_renderer_t subcommand;
    diagnostic_renderer_t diagnostic;

    Renderer();
};

struct CompatibleRenderer : Renderer {
    explicit CompatibleRenderer(TextStyle style = {});
};

struct ModernRenderer : Renderer {
    ModernRenderer();
    explicit ModernRenderer(TextStyle style);
};

auto default_text_style() -> const TextStyle&;

void set_default_text_style(TextStyle style);

auto default_renderer() -> const Renderer&;

void set_default_renderer(Renderer renderer);

auto resolve_renderer(const Renderer* renderer) -> const Renderer&;

auto render_usage(const UsageDocument& document,
                  bool include_help = true,
                  const Renderer* renderer = nullptr) -> std::string;

auto render_subcommands(const SubCommandDocument& document,
                        const Renderer* renderer = nullptr) -> std::string;

auto render_diagnostic(const Diagnostic& diagnostic,
                       const Renderer* renderer = nullptr) -> std::string;

}  // namespace deco::cli::text
