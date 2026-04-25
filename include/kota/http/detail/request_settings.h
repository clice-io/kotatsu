#pragma once

#include <chrono>
#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "kota/http/detail/common.h"
#include "kota/http/detail/curl.h"
#include "kota/http/detail/util.h"

namespace kota::http::detail {

template <typename T>
curl_option_hook make_curl_option(CURLoption option, T&& value) {
    using stored_t = std::decay_t<T>;
    static_assert(std::is_copy_constructible_v<stored_t>,
                  "native curl option values must be copy constructible");

    if constexpr(std::same_as<stored_t, std::string>) {
        return [option, value = std::move(value)](CURL* easy) -> curl::easy_error {
            return curl::setopt(easy, option, value.c_str());
        };
    } else if constexpr(std::same_as<stored_t, std::string_view>) {
        std::string owned(value);
        return [option, owned = std::move(owned)](CURL* easy) -> curl::easy_error {
            return curl::setopt(easy, option, owned.c_str());
        };
    } else {
        return [option, value = std::forward<T>(value)](CURL* easy) -> curl::easy_error {
            return curl::setopt(easy, option, value);
        };
    }
}

class request_settings {
public:
    request_settings() = default;
    request_settings(const request_settings&) = default;
    request_settings(request_settings&&) noexcept = default;
    request_settings& operator=(const request_settings&) = default;
    request_settings& operator=(request_settings&&) noexcept = default;

    decltype(auto) header(this auto&& self, std::string name, std::string value) {
        detail::upsert_header(self.header_list, std::move(name), std::move(value));
        return std::forward<decltype(self)>(self);
    }

    decltype(auto) default_header(this auto&& self, std::string name, std::string value) {
        detail::insert_header(self.header_list, std::move(name), std::move(value));
        return std::forward<decltype(self)>(self);
    }

    decltype(auto) cookies(this auto&& self, std::string value) {
        self.cookie_string = std::move(value);
        return std::forward<decltype(self)>(self);
    }

    decltype(auto) user_agent(this auto&& self, std::string value) {
        self.user_agent_value = std::move(value);
        return std::forward<decltype(self)>(self);
    }

    decltype(auto) proxy(this auto&& self, http::proxy value) {
        self.proxy_config = std::move(value);
        self.disable_proxy = false;
        return std::forward<decltype(self)>(self);
    }

    decltype(auto) proxy(this auto&& self, std::string url) {
        return std::forward<decltype(self)>(self).proxy(http::proxy{
            .url = std::move(url),
            .username = {},
            .password = {},
        });
    }

    decltype(auto) no_proxy(this auto&& self) {
        self.proxy_config.reset();
        self.disable_proxy = true;
        return std::forward<decltype(self)>(self);
    }

    decltype(auto) timeout(this auto&& self, std::chrono::milliseconds value) {
        self.timeout_value = value;
        return std::forward<decltype(self)>(self);
    }

    template <typename T>
    decltype(auto) curl_option(this auto&& self, CURLoption option, T&& value) {
        self.curl_options.push_back(detail::make_curl_option(option, std::forward<T>(value)));
        return std::forward<decltype(self)>(self);
    }

    decltype(auto) redirect(this auto&& self, redirect_policy value) {
        self.redirect_policy_value = value;
        return std::forward<decltype(self)>(self);
    }

    decltype(auto) referer(this auto&& self, bool enabled) {
        self.redirect_policy_value.referer = enabled;
        return std::forward<decltype(self)>(self);
    }

    decltype(auto) https_only(this auto&& self, bool enabled = true) {
        self.tls_config.https_only = enabled;
        return std::forward<decltype(self)>(self);
    }

    decltype(auto) danger_accept_invalid_certs(this auto&& self, bool enabled = true) {
        self.tls_config.danger_accept_invalid_certs = enabled;
        return std::forward<decltype(self)>(self);
    }

    decltype(auto) danger_accept_invalid_hostnames(this auto&& self, bool enabled = true) {
        self.tls_config.danger_accept_invalid_hostnames = enabled;
        return std::forward<decltype(self)>(self);
    }

    decltype(auto) min_tls_version(this auto&& self, http::tls_version value) {
        self.tls_config.min_version = value;
        return std::forward<decltype(self)>(self);
    }

    decltype(auto) max_tls_version(this auto&& self, http::tls_version value) {
        self.tls_config.max_version = value;
        return std::forward<decltype(self)>(self);
    }

    decltype(auto) ca_file(this auto&& self, std::string path) {
        self.tls_config.ca_file = std::move(path);
        return std::forward<decltype(self)>(self);
    }

    decltype(auto) ca_path(this auto&& self, std::string path) {
        self.tls_config.ca_path = std::move(path);
        return std::forward<decltype(self)>(self);
    }

    decltype(auto) record_cookie(this auto&& self, bool enabled = true) noexcept {
        self.record_cookie_enabled = enabled;
        return std::forward<decltype(self)>(self);
    }

protected:
    std::vector<http::header> header_list{};
    std::string cookie_string{};
    std::string user_agent_value{};
    std::optional<http::proxy> proxy_config{};
    redirect_policy redirect_policy_value = redirect_policy::limited();
    http::tls_options tls_config{};
    std::optional<std::chrono::milliseconds> timeout_value{};
    std::vector<curl_option_hook> curl_options{};
    bool record_cookie_enabled = true;
    bool disable_proxy = false;
};

}  // namespace kota::http::detail
