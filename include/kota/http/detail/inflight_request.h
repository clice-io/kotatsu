#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "kota/http/detail/curl.h"
#include "kota/http/detail/request.h"
#include "kota/http/detail/response.h"
#include "kota/async/vocab/error.h"
#include "kota/async/vocab/outcome.h"

namespace kota::http::detail {

class inflight_request : public request_settings {
public:
    explicit inflight_request(http::request request) noexcept;

    bool fail(error err) noexcept;
    bool fail(curl::easy_error code) noexcept;
    bool prepare() noexcept;
    bool bind_runtime(void* opaque) noexcept;
    void clear_runtime_binding() noexcept;
    outcome<response, error, cancellation> finish() noexcept;

    static std::size_t on_write(char* data, std::size_t size, std::size_t count, void* userdata);
    static std::size_t on_header(char* data, std::size_t size, std::size_t count, void* userdata);

    std::shared_ptr<shared_resources> shared;
    std::string method_name;
    std::string url_string;
    std::vector<query_param> query_params;
    std::string body_text;
    curl::easy_handle easy;
    curl::slist header_lines;
    response out{};
    error result{};
    std::string final_url;

private:
    bool apply_url() noexcept;
    bool apply_method() noexcept;
    bool apply_body() noexcept;
    bool apply_headers() noexcept;
    bool apply_cookies() noexcept;
    bool apply_user_agent() noexcept;
    bool apply_redirect() noexcept;
    bool apply_tls() noexcept;
    bool apply_proxy() noexcept;
    bool apply_timeout() noexcept;
    bool apply_curl_options() noexcept;
};

template <typename T>
bool easy_setopt(inflight_request& request, CURLoption option, T value) noexcept {
    if(auto err = curl::setopt(request.easy.get(), option, value); !curl::ok(err)) {
        return request.fail(err);
    }
    return true;
}

}  // namespace kota::http::detail
