#include "kota/deco/facade/text.h"

#include <algorithm>
#include <format>
#include <utility>

#include "kota/deco/facade/config.h"

namespace kota::deco::cli::text {
namespace {

constexpr std::string_view ansi_reset = "\033[0m";
constexpr std::string_view ansi_bold = "\033[1m";
constexpr std::string_view ansi_usage_heading = "\033[1;38;5;81m";
constexpr std::string_view ansi_usage_syntax = "\033[1;38;5;117m";
constexpr std::string_view ansi_group = "\033[1;38;5;110m";
constexpr std::string_view ansi_group_title = "\033[1;4;38;5;110m";
constexpr std::string_view ansi_help = "\033[39m";
constexpr std::string_view ansi_alias = "\033[38;5;150m";
constexpr std::string_view ansi_error = "\033[1;38;5;203m";
constexpr std::string_view ansi_marker = "\033[1;38;5;204m";
constexpr std::string_view ansi_context = "\033[39m";

struct JoinedArgv {
    std::string line;
    std::vector<std::size_t> starts;
};

struct DiagnosticExcerpt {
    std::string line;
    std::size_t marker_start = 0;
    std::size_t marker_width = 1;
};

struct DiagnosticLayout {
    unsigned begin = 0;
    bool points_to_end = false;
    DiagnosticExcerpt excerpt;

    auto compatible_label() const -> std::string {
        return points_to_end ? "at end of argv" : std::format("at argv[{}]", begin);
    }

    auto modern_label() const -> std::string {
        return points_to_end ? "argv[end]" : std::format("argv[{}]", begin);
    }
};

auto paint(std::string_view ansi, std::string_view text) -> std::string;
auto join_argv(std::span<const std::string> argv) -> JoinedArgv;
auto build_diagnostic_layout(const Diagnostic& diagnostic, std::size_t max_width)
    -> DiagnosticLayout;
auto excerpt_diagnostic_line(std::string_view line,
                             std::size_t marker_start,
                             std::size_t marker_width,
                             std::size_t max_width) -> DiagnosticExcerpt;
auto highlight_span(std::string_view text,
                    std::size_t start,
                    std::size_t width,
                    std::string_view ansi) -> std::string;
auto modern_heading(std::string_view title, std::string_view body) -> std::string;

struct CompatibleRendererImpl {
    static auto render_usage_entry(const UsageEntry& entry,
                                   bool include_help,
                                   const TextStyle& style) -> std::string {
        return include_help ? text::render_usage_entry(entry, style.usage) : "  " + entry.usage;
    }

    static auto render_usage_document(const UsageDocument& document,
                                      bool include_help,
                                      const TextStyle& style) -> std::string {
        std::string rendered =
            std::format("usage: {}\n\n{}\n", document.overview, style.usage.options_heading);

        auto append_entries = [&](std::span<const UsageEntry> entries) {
            for(const auto& entry: entries) {
                rendered += render_usage_entry(entry, include_help, style);
                rendered.push_back('\n');
            }
        };

        const bool single_default_group =
            document.groups.size() == 1 && document.groups.front().is_default;
        if(!style.usage.group_by_category || single_default_group) {
            for(const auto& group: document.groups) {
                append_entries(group.entries);
            }
            return rendered;
        }

        for(const auto& group: document.groups) {
            rendered += style.usage.group_prefix;
            rendered += group.title;
            if(group.exclusive) {
                rendered += style.usage.exclusive_suffix;
            }
            rendered += ":\n";
            append_entries(group.entries);
            rendered.push_back('\n');
        }
        return rendered;
    }

    static auto render_subcommand_document(const SubCommandDocument& document,
                                           const TextStyle& style) -> std::string {
        std::string rendered;

        if(style.subcommand.show_overview && !document.overview.empty()) {
            rendered += document.overview;
            rendered += "\n\n";
        }

        if(style.subcommand.show_usage_line && document.has_usage_line) {
            rendered += "usage: ";
            rendered += document.usage_line;
            rendered.push_back('\n');
            if(!document.entries.empty()) {
                rendered.push_back('\n');
            }
        }

        if(document.entries.empty()) {
            return rendered;
        }

        rendered += style.subcommand.heading;
        rendered.push_back('\n');

        std::size_t max_name_len = 0;
        if(style.subcommand.align_description) {
            for(const auto& item: document.entries) {
                max_name_len = std::max(max_name_len, item.name.size());
            }
        }

        for(const auto& item: document.entries) {
            rendered += "  ";
            rendered += item.name;

            if(style.subcommand.show_description && !item.description.empty()) {
                if(style.subcommand.align_description) {
                    rendered.append(max_name_len - item.name.size() + 2, ' ');
                } else {
                    rendered += "  ";
                }
                rendered += item.description;
            }

            if(style.subcommand.show_command_alias && item.command != item.name) {
                rendered += " (";
                rendered += item.command;
                rendered.push_back(')');
            }

            rendered.push_back('\n');
        }

        return rendered;
    }

    static auto render_diagnostic_document(const Diagnostic& diagnostic, const TextStyle& style)
        -> std::string {
        if(!style.diagnostic.enabled || !diagnostic.positioned) {
            return diagnostic.message;
        }

        const auto layout = build_diagnostic_layout(diagnostic, style.diagnostic.max_source_width);

        if(!style.diagnostic.show_source_line || diagnostic.argv.empty()) {
            if(style.diagnostic.show_label) {
                return std::format("{}:\n  {}", layout.compatible_label(), diagnostic.message);
            }
            return diagnostic.message;
        }

        std::string marker(layout.excerpt.marker_start, ' ');
        marker.push_back(style.diagnostic.pointer);
        if(layout.excerpt.marker_width > 1) {
            marker.append(layout.excerpt.marker_width - 1, style.diagnostic.underline);
        }

        if(style.diagnostic.show_label) {
            return std::format("{}:\n  {}\n  {}\n  {}",
                               layout.compatible_label(),
                               layout.excerpt.line,
                               marker,
                               diagnostic.message);
        }
        return std::format("{}\n{}\n{}", layout.excerpt.line, marker, diagnostic.message);
    }
};

struct ModernRendererImpl {
    static auto render_usage_entry(const UsageEntry& entry,
                                   bool include_help,
                                   const TextStyle& style) -> std::string {
        const auto syntax = paint(ansi_usage_syntax, entry.usage);
        if(!include_help) {
            return "  " + syntax;
        }

        const auto help_text =
            entry.help.empty() ? std::string(style.usage.default_help) : entry.help;
        const auto help = paint(ansi_help, help_text);
        if(entry.usage.size() >= style.usage.help_column) {
            return "  " + syntax + "\n" + std::string(style.usage.help_column, ' ') + "  " + help;
        }
        return "  " + syntax + std::string(style.usage.help_column - entry.usage.size(), ' ') +
               help;
    }

    static auto render_usage_document(const UsageDocument& document,
                                      bool include_help,
                                      const TextStyle& style) -> std::string {
        std::string rendered = modern_heading("Usage", document.overview);
        rendered += "\n\n";
        rendered += paint(ansi_usage_heading, style.usage.options_heading);
        rendered.push_back('\n');

        auto append_entries = [&](std::span<const UsageEntry> entries) {
            for(const auto& entry: entries) {
                rendered += render_usage_entry(entry, include_help, style);
                rendered.push_back('\n');
            }
        };

        const bool single_default_group =
            document.groups.size() == 1 && document.groups.front().is_default;
        if(!style.usage.group_by_category || single_default_group) {
            for(const auto& group: document.groups) {
                append_entries(group.entries);
            }
            return rendered;
        }

        for(const auto& group: document.groups) {
            rendered += "  ";
            rendered += paint(ansi_group, "◆");
            rendered.push_back(' ');
            rendered += paint(ansi_group_title, group.title);
            if(group.exclusive) {
                rendered += paint(ansi_help, style.usage.exclusive_suffix);
            }
            rendered.push_back('\n');
            append_entries(group.entries);
            rendered.push_back('\n');
        }
        return rendered;
    }

    static auto render_subcommand_document(const SubCommandDocument& document,
                                           const TextStyle& style) -> std::string {
        std::string rendered;

        if(style.subcommand.show_overview && !document.overview.empty()) {
            rendered += paint(ansi_bold, document.overview);
            rendered += "\n\n";
        }

        if(style.subcommand.show_usage_line && document.has_usage_line) {
            rendered += modern_heading("Usage", document.usage_line);
            rendered += "\n\n";
        }

        if(document.entries.empty()) {
            return rendered;
        }

        rendered += paint(ansi_usage_heading, style.subcommand.heading);
        rendered.push_back('\n');

        std::size_t max_name_len = 0;
        if(style.subcommand.align_description) {
            for(const auto& item: document.entries) {
                max_name_len = std::max(max_name_len, item.name.size());
            }
        }

        for(const auto& item: document.entries) {
            rendered += "  ";
            rendered += paint(ansi_usage_syntax, item.name);

            if(style.subcommand.show_description && !item.description.empty()) {
                if(style.subcommand.align_description) {
                    rendered.append(max_name_len - item.name.size() + 2, ' ');
                } else {
                    rendered += "  ";
                }
                rendered += paint(ansi_help, item.description);
            }

            if(style.subcommand.show_command_alias && item.command != item.name) {
                rendered.push_back(' ');
                rendered += paint(ansi_alias, std::format("({})", item.command));
            }

            rendered.push_back('\n');
        }

        return rendered;
    }

    static auto render_diagnostic_document(const Diagnostic& diagnostic, const TextStyle& style)
        -> std::string {
        if(!style.diagnostic.enabled) {
            return diagnostic.message;
        }

        std::string rendered = paint(ansi_error, "error");

        if(!diagnostic.positioned || diagnostic.argv.empty()) {
            rendered += " ";
            rendered += paint(ansi_bold, diagnostic.message);
            return rendered;
        }

        const auto layout = build_diagnostic_layout(diagnostic, style.diagnostic.max_source_width);

        if(style.diagnostic.show_label) {
            rendered += " ";
            rendered += paint(ansi_context, std::format("[{}]", layout.modern_label()));
        }

        if(!style.diagnostic.show_source_line) {
            rendered += " ";
            rendered += paint(ansi_bold, diagnostic.message);
            return rendered;
        }

        rendered += "\n";
        rendered += paint(ansi_context, "│");
        rendered += "\n";
        rendered += paint(ansi_context, "│");
        rendered.push_back(' ');
        rendered += highlight_span(layout.excerpt.line,
                                   layout.excerpt.marker_start,
                                   layout.excerpt.marker_width,
                                   ansi_usage_syntax);
        rendered += "\n";
        rendered += paint(ansi_context, "│");
        rendered.push_back(' ');
        rendered.append(layout.excerpt.marker_start, ' ');
        rendered += paint(ansi_marker, "╰─▶");
        rendered.push_back(' ');
        rendered += paint(ansi_bold, diagnostic.message);
        return rendered;
    }
};

auto paint(std::string_view ansi, std::string_view text) -> std::string {
    if(text.empty()) {
        return {};
    }
    return std::format("{}{}{}", ansi, text, ansi_reset);
}

auto join_argv(std::span<const std::string> argv) -> JoinedArgv {
    JoinedArgv joined;
    joined.line.reserve(argv.size() * 8);
    joined.starts.reserve(argv.size());

    std::size_t cursor = 0;
    for(std::size_t i = 0; i < argv.size(); ++i) {
        joined.starts.push_back(cursor);
        joined.line += argv[i];
        cursor += argv[i].size();
        if(i + 1 < argv.size()) {
            joined.line.push_back(' ');
            ++cursor;
        }
    }
    return joined;
}

auto build_diagnostic_layout(const Diagnostic& diagnostic, std::size_t max_width)
    -> DiagnosticLayout {
    const auto argc = static_cast<unsigned>(diagnostic.argv.size());
    const unsigned begin = std::min<unsigned>(diagnostic.begin, argc);
    unsigned end = std::min<unsigned>(diagnostic.end, argc);
    const bool points_to_end = begin >= diagnostic.argv.size();
    if(end <= begin) {
        end = begin + 1;
    }

    const auto joined = join_argv(diagnostic.argv);
    std::size_t marker_start = joined.line.size();
    std::size_t marker_width = 1;
    if(!points_to_end) {
        marker_start = joined.starts[begin];
        marker_width = joined.starts[end - 1] + diagnostic.argv[end - 1].size() - marker_start;
    }

    return DiagnosticLayout{
        .begin = begin,
        .points_to_end = points_to_end,
        .excerpt = excerpt_diagnostic_line(joined.line, marker_start, marker_width, max_width),
    };
}

auto excerpt_diagnostic_line(std::string_view line,
                             std::size_t marker_start,
                             std::size_t marker_width,
                             std::size_t max_width) -> DiagnosticExcerpt {
    DiagnosticExcerpt excerpt{
        .line = std::string(line),
        .marker_start = marker_start,
        .marker_width = marker_width,
    };

    if(max_width == 0 || line.size() <= max_width) {
        return excerpt;
    }

    constexpr std::string_view ellipsis = "...";
    if(max_width <= ellipsis.size()) {
        excerpt.line = std::string(ellipsis.substr(0, max_width));
        excerpt.marker_start = 0;
        excerpt.marker_width = 1;
        return excerpt;
    }

    // Up to 16 characters before the marker, fewer when the width leaves room for less
    // between an ellipsis on either side, so that the marker stays in view.
    const std::size_t between_ellipses =
        max_width > 2 * ellipsis.size() ? max_width - 2 * ellipsis.size() : 0;
    const std::size_t context_before_marker =
        std::min<std::size_t>(16, between_ellipses == 0 ? 0 : between_ellipses - 1);

    std::size_t content_start = 0;
    if(marker_start >= line.size()) {
        content_start = line.size() - (max_width - ellipsis.size());
    } else if(marker_start > context_before_marker) {
        content_start = marker_start - context_before_marker;
    }

    bool crop_left = content_start > 0;
    std::size_t available = max_width;
    if(crop_left) {
        available -= ellipsis.size();
    }
    std::size_t content_end = std::min(line.size(), content_start + available);
    bool crop_right = content_end < line.size();
    if(crop_right) {
        if(available > ellipsis.size()) {
            available -= ellipsis.size();
            content_end = content_start + available;
        } else {
            // No room for a second ellipsis: the line just stops.
            crop_right = false;
        }
    }

    if(content_end >= line.size()) {
        crop_right = false;
        available = max_width;
        if(crop_left) {
            available -= ellipsis.size();
        }
        content_start = line.size() > available ? line.size() - available : 0;
        crop_left = content_start > 0;
        content_end = line.size();
    }

    std::string rendered;
    rendered.reserve(max_width);
    if(crop_left) {
        rendered += ellipsis;
    }
    rendered += line.substr(content_start, content_end - content_start);
    if(crop_right) {
        rendered += ellipsis;
    }

    const std::size_t visible_marker_start =
        marker_start <= content_start ? 0 : marker_start - content_start;
    const std::size_t prefix_width = crop_left ? ellipsis.size() : 0;
    const std::size_t visible_content_width =
        content_end > marker_start ? content_end - std::max(marker_start, content_start) : 0;

    excerpt.line = std::move(rendered);
    excerpt.marker_start = prefix_width + visible_marker_start;
    excerpt.marker_width = std::max<std::size_t>(1, std::min(marker_width, visible_content_width));
    return excerpt;
}

auto highlight_span(std::string_view text,
                    std::size_t start,
                    std::size_t width,
                    std::string_view ansi) -> std::string {
    if(start >= text.size() || width == 0) {
        return std::string(text);
    }

    width = std::min(width, text.size() - start);
    std::string rendered;
    rendered.reserve(text.size() + ansi.size() + ansi_reset.size() + 8);
    rendered += text.substr(0, start);
    rendered += ansi;
    rendered += text.substr(start, width);
    rendered += ansi_reset;
    rendered += text.substr(start + width);
    return rendered;
}

auto modern_heading(std::string_view title, std::string_view body) -> std::string {
    std::string rendered = paint(ansi_usage_heading, title);
    if(!body.empty()) {
        rendered.push_back(' ');
        rendered += paint(ansi_bold, body);
    }
    return rendered;
}

template <typename RendererConfig>
auto text_style_of(const RendererConfig& config) -> TextStyle {
    return TextStyle{
        .diagnostic = config.diagnostic,
        .usage = config.usage,
        .subcommand = config.subcommand,
    };
}

auto global_config() -> config::Config& {
    static thread_local config::Config current{};
    return current;
}

/// The renderer the global config makes, which renders when no default renderer is set.
auto config_renderer() -> Renderer& {
    static thread_local Renderer renderer = CompatibleRenderer(global_config().render.compatible);
    return renderer;
}

auto explicit_renderer() -> std::optional<Renderer>& {
    static thread_local std::optional<Renderer> renderer;
    return renderer;
}

}  // namespace

auto looks_like_rendered_diagnostic([[maybe_unused]] std::string_view text) -> bool {
    return false;
}

auto diagnostic_at(std::span<const std::string> argv,
                   unsigned begin,
                   unsigned end,
                   std::string message) -> Diagnostic {
    return Diagnostic{
        .message = std::move(message),
        .argv = argv,
        .begin = begin,
        .end = end,
        .positioned = true,
    };
}

auto diagnostic_message(std::string message) -> Diagnostic {
    return Diagnostic{
        .message = std::move(message),
    };
}

Renderer::Renderer() = default;

CompatibleRenderer::CompatibleRenderer() : CompatibleRenderer(CompatibleRendererConfig{}) {}

CompatibleRenderer::CompatibleRenderer(CompatibleRendererConfig config) : Renderer() {
    style = text_style_of(config);
    usage = [](const UsageDocument& document, bool include_help, const TextStyle& active_style) {
        return CompatibleRendererImpl::render_usage_document(document, include_help, active_style);
    };
    subcommand = [](const SubCommandDocument& document, const TextStyle& active_style) {
        return CompatibleRendererImpl::render_subcommand_document(document, active_style);
    };
    diagnostic = [](const Diagnostic& diag, const TextStyle& active_style) {
        return CompatibleRendererImpl::render_diagnostic_document(diag, active_style);
    };
}

ModernRenderer::ModernRenderer() : ModernRenderer(ModernRendererConfig{}) {}

ModernRenderer::ModernRenderer(ModernRendererConfig config) : Renderer() {
    style = text_style_of(config);
    usage = [](const UsageDocument& document, bool include_help, const TextStyle& active_style) {
        return ModernRendererImpl::render_usage_document(document, include_help, active_style);
    };
    subcommand = [](const SubCommandDocument& document, const TextStyle& active_style) {
        return ModernRendererImpl::render_subcommand_document(document, active_style);
    };
    diagnostic = [](const Diagnostic& diag, const TextStyle& active_style) {
        return ModernRendererImpl::render_diagnostic_document(diag, active_style);
    };
}

auto explicit_default_renderer() -> const Renderer* {
    const auto& renderer = explicit_renderer();
    return renderer.has_value() ? &*renderer : nullptr;
}

auto default_renderer() -> const Renderer& {
    if(const auto* renderer = explicit_default_renderer()) {
        return *renderer;
    }
    return config_renderer();
}

void set_default_renderer(Renderer renderer) {
    explicit_renderer() = std::move(renderer);
}

void clear_default_renderer() {
    explicit_renderer().reset();
}

auto resolve_renderer(const Renderer* renderer) -> const Renderer& {
    if(renderer != nullptr) {
        return *renderer;
    }
    return default_renderer();
}

auto render_usage(const UsageDocument& document, bool include_help, const Renderer* renderer)
    -> std::string {
    const auto& active_renderer = resolve_renderer(renderer);
    if(active_renderer.usage) {
        return active_renderer.usage(document, include_help, active_renderer.style);
    }
    return CompatibleRendererImpl::render_usage_document(document,
                                                         include_help,
                                                         active_renderer.style);
}

auto render_usage_entry(const UsageEntry& entry, const UsageStyle& style) -> std::string {
    const std::string_view help = entry.help.empty() ? style.default_help : entry.help;
    if(entry.usage.size() >= style.help_column) {
        return std::format("  {}\n  {:{}}{}", entry.usage, "", style.help_column, help);
    }
    return std::format("  {:{}}{}", entry.usage, style.help_column, help);
}

auto render_subcommands(const SubCommandDocument& document, const Renderer* renderer)
    -> std::string {
    const auto& active_renderer = resolve_renderer(renderer);
    if(active_renderer.subcommand) {
        return active_renderer.subcommand(document, active_renderer.style);
    }
    return CompatibleRendererImpl::render_subcommand_document(document, active_renderer.style);
}

auto render_diagnostic(const Diagnostic& diagnostic, const Renderer* renderer) -> std::string {
    const auto& active_renderer = resolve_renderer(renderer);
    if(active_renderer.diagnostic) {
        return active_renderer.diagnostic(diagnostic, active_renderer.style);
    }
    return CompatibleRendererImpl::render_diagnostic_document(diagnostic, active_renderer.style);
}

}  // namespace kota::deco::cli::text

namespace kota::deco::config {

auto get() -> const Config& {
    return cli::text::global_config();
}

void set(Config config) {
    cli::text::global_config() = std::move(config);
    cli::text::config_renderer() = cli::text::CompatibleRenderer(get().render.compatible);
}

void set_render(BuiltInRenderConfig render) {
    auto updated = get();
    updated.render = std::move(render);
    set(std::move(updated));
}

void set_enum_meta_var(EnumMetaVarConfig config) {
    auto updated = get();
    updated.enum_meta_var = std::move(config);
    set(std::move(updated));
}

}  // namespace kota::deco::config
