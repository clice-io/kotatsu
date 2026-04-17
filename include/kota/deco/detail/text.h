#pragma once

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "./config.h"

namespace kota::deco::cli::text {

using PositionStyle = ::kota::deco::config::PositionStyle;
using UsageStyle = ::kota::deco::config::UsageStyle;
using SubCommandStyle = ::kota::deco::config::SubCommandStyle;
using TextStyle = ::kota::deco::config::TextStyle;
using CompatibleRendererConfig = ::kota::deco::config::CompatibleRendererConfig;
using ModernRendererConfig = ::kota::deco::config::ModernRendererConfig;

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
    using diagnostic_renderer_t = std::function<std::string(const Diagnostic&, const TextStyle&)>;

    TextStyle style{};
    usage_renderer_t usage;
    subcommand_renderer_t subcommand;
    diagnostic_renderer_t diagnostic;

    Renderer();
};

struct CompatibleRenderer : Renderer {
    CompatibleRenderer();
    explicit CompatibleRenderer(CompatibleRendererConfig config);
};

struct ModernRenderer : Renderer {
    ModernRenderer();
    explicit ModernRenderer(ModernRendererConfig config);
};

auto explicit_default_renderer() -> const Renderer*;

auto default_renderer() -> const Renderer&;

void set_default_renderer(Renderer renderer);
void clear_default_renderer();

auto resolve_renderer(const Renderer* renderer) -> const Renderer&;

auto render_usage(const UsageDocument& document,
                  bool include_help = true,
                  const Renderer* renderer = nullptr) -> std::string;

auto render_subcommands(const SubCommandDocument& document, const Renderer* renderer = nullptr)
    -> std::string;

auto render_diagnostic(const Diagnostic& diagnostic, const Renderer* renderer = nullptr)
    -> std::string;

}  // namespace kota::deco::cli::text
