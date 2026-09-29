#pragma once

#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "kota/deco/deco.h"

namespace kota::test {

/// Restores deco's global config, and the renderer it makes, when it goes out of scope.
struct ScopedDecoConfig {
    deco::config::Config saved = deco::config::get();

    ScopedDecoConfig() = default;
    ScopedDecoConfig(const ScopedDecoConfig&) = delete;
    auto operator=(const ScopedDecoConfig&) -> ScopedDecoConfig& = delete;

    ~ScopedDecoConfig() {
        deco::config::set(saved);
    }
};

/// Restores the default renderer, set or not, when it goes out of scope.
struct ScopedDefaultRenderer {
    std::optional<deco::cli::text::Renderer> saved =
        deco::cli::text::explicit_default_renderer() != nullptr
            ? std::optional(*deco::cli::text::explicit_default_renderer())
            : std::nullopt;

    ScopedDefaultRenderer() = default;
    ScopedDefaultRenderer(const ScopedDefaultRenderer&) = delete;
    auto operator=(const ScopedDefaultRenderer&) -> ScopedDefaultRenderer& = delete;

    ~ScopedDefaultRenderer() {
        if(saved.has_value()) {
            deco::cli::text::set_default_renderer(*saved);
        } else {
            deco::cli::text::clear_default_renderer();
        }
    }
};

/// `text` with its escape characters written "\e", so that a snapshot of styled text reads.
inline std::string visible(std::string_view text) {
    std::string out;
    for(const char ch: text) {
        if(ch == '\033') {
            out += R"(\e)";
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

/// A renderer that writes what it is given in brackets, so that a test sees which renderer
/// rendered and with what: `USAGE<overview:help>`, `SUB<usage line:entries>`,
/// `ERR<begin:message>`.
inline deco::cli::text::Renderer tagged_renderer() {
    using namespace deco::cli::text;
    Renderer renderer;
    renderer.usage = [](const UsageDocument& document, bool include_help, const TextStyle&) {
        return std::format("USAGE<{}:{}>", document.overview, include_help ? "help" : "plain");
    };
    renderer.subcommand = [](const SubCommandDocument& document, const TextStyle&) {
        return std::format("SUB<{}:{}>", document.usage_line, document.entries.size());
    };
    renderer.diagnostic = [](const Diagnostic& diagnostic, const TextStyle&) {
        return std::format("ERR<{}:{}>", diagnostic.begin, diagnostic.message);
    };
    return renderer;
}

}  // namespace kota::test
