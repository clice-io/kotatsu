#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "kota/http/detail/common.h"

namespace kota::http {

/// What a request received: an error status is a response too.
struct response {
    int status = 0;
    /// Where the response came from, after the redirects followed.
    std::string url;
    /// The headers of the last response, redirects and interim responses
    /// left out, in the order they came.
    std::vector<header> headers;
    std::vector<std::byte> body;

    /// Whether the status is 2xx.
    bool ok() const noexcept {
        return 200 <= status && status < 300;
    }

    std::span<const std::byte> bytes() const noexcept;

    /// The body as text, viewed in place.
    std::string_view text() const noexcept;

    std::string text_copy() const;

    /// The value of the first header named `name`, in any case.
    std::optional<std::string_view> header_value(std::string_view name) const noexcept;
};

}  // namespace kota::http
