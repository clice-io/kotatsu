#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "kota/deco/deco.h"

namespace kota::test {

/// GET or POST, and nothing else.
struct WebMethod {
    enum class Kind : std::uint8_t {
        Get,
        Post,
    };

    Kind kind = Kind::Get;

    std::optional<std::string> into(std::string_view input) {
        if(input == "GET" || input == "POST") {
            kind = input == "GET" ? Kind::Get : Kind::Post;
            return std::nullopt;
        }
        return "Invalid request type. Expected 'GET' or 'POST'.";
    }
};

/// An http:// or https:// URL.
struct WebUrl {
    std::string url;

    std::optional<std::string> into(std::string_view input) {
        if(input.starts_with("http://") || input.starts_with("https://")) {
            url = input;
            return std::nullopt;
        }
        return "Invalid URL. Expected to start with 'http://' or 'https://'.";
    }
};

/// The options of a small web client, in three exclusive categories: asking for its
/// version, for help, or making a request, which needs a method and a URL.
struct WebCli {
    constexpr static deco::decl::Category version_category{
        .exclusive = true,
        .required = false,
        .name = "version",
        .description = "version-only mode",
    };

    constexpr static deco::decl::Category help_category{
        .exclusive = true,
        .required = false,
        .name = "help",
        .description = "help-only mode",
    };

    constexpr static deco::decl::Category request_category{
        .exclusive = true,
        .required = false,
        .name = "request",
        .description = "request options",
    };

    struct Version {
        DecoFlag(names = {"-v", "--version"}; help = "Show version and exit";)
        version;
    };

    struct Request {
        DecoFlag(help = "Enable verbose output";)
        verbose = false;

        DecoKV(names = {"-X", "--type"}; meta_var = "<Method>";)
        <WebMethod> method;

        DecoKV(meta_var = "<URL>"; help = "Request URL";)
        <WebUrl> url;
    };

    struct Help {
        DecoFlag(names = {"-h", "--help"}; help = "Show this help message and exit";)
        help;
    };

    DECO_CFG(required = false; category = version_category);
    Version version;

    DECO_CFG(required = true; category = request_category);
    Request request;

    DECO_CFG(required = false; category = help_category);
    Help help;
};

}  // namespace kota::test
