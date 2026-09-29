#include "kota/http/detail/response.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "kota/http/detail/util.h"

namespace kota::http {

std::string error::message() const {
    return http::message(*this);
}

std::string message(const error& err) {
    if(!err.detail.empty()) {
        return err.detail;
    }
    switch(err.kind) {
        case error_kind::curl: return std::string(curl::message(err.curl_code));
        case error_kind::invalid_request: return "invalid http request";
        case error_kind::json_encode: return "json encode failed";
        case error_kind::aborted: return "http request aborted";
    }
    std::unreachable();
}

std::span<const std::byte> response::bytes() const noexcept {
    return body;
}

std::string_view response::text() const noexcept {
    if(body.empty()) {
        return {};
    }
    auto* ptr = reinterpret_cast<const char*>(body.data());
    return std::string_view(ptr, body.size());
}

std::string response::text_copy() const {
    auto view = text();
    return std::string(view.data(), view.size());
}

std::optional<std::string_view> response::header_value(std::string_view name) const noexcept {
    for(const auto& item: headers) {
        if(detail::iequals(item.name, name)) {
            return item.value;
        }
    }
    return std::nullopt;
}

}  // namespace kota::http
