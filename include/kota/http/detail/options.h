#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "kota/http/detail/common.h"

namespace kota::http {

struct client_options {
    std::vector<header> default_headers = {};
    std::string default_cookies = {};
    std::optional<proxy> proxy_config = {};
    std::string user_agent = {};
    redirect_policy redirect = redirect_policy::limited();
    tls_options tls{};
    std::optional<std::chrono::milliseconds> timeout = {};
    std::vector<curl_option_hook> curl_options = {};
    bool record_cookie = true;
    bool disable_proxy = false;
};

}  // namespace kota::http
