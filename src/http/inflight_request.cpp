#include "kota/http/detail/inflight_request.h"

#include <cassert>
#include <cstddef>
#include <limits>
#include <string_view>
#include <utility>

#include "kota/http/detail/util.h"

namespace kota::http {

namespace {

constexpr int tls_version_rank(http::tls_version value) noexcept {
    switch(value) {
        case http::tls_version::tls1_0: return 10;
        case http::tls_version::tls1_1: return 11;
        case http::tls_version::tls1_2: return 12;
        case http::tls_version::tls1_3: return 13;
    }

    return 0;
}

}  // namespace

namespace detail {

namespace {

bool bind_easy(CURL* easy,
               const std::shared_ptr<shared_resources>& shared,
               bool enable_record_cookie) noexcept {
    if(!easy || !shared || !shared->share) {
        return false;
    }

    if(auto err = curl::setopt(easy, CURLOPT_SHARE, shared->share.get()); !curl::ok(err)) {
        return false;
    }

    if(enable_record_cookie) {
        if(auto err = curl::setopt(easy, CURLOPT_COOKIEFILE, ""); !curl::ok(err)) {
            return false;
        }
    }

    return true;
}

}  // namespace

long to_curl_ssl_min(http::tls_version value) noexcept {
    switch(value) {
        case http::tls_version::tls1_0: return CURL_SSLVERSION_TLSv1_0;
        case http::tls_version::tls1_1: return CURL_SSLVERSION_TLSv1_1;
        case http::tls_version::tls1_2: return CURL_SSLVERSION_TLSv1_2;
        case http::tls_version::tls1_3: return CURL_SSLVERSION_TLSv1_3;
    }

    return CURL_SSLVERSION_DEFAULT;
}

long to_curl_ssl_max(http::tls_version value) noexcept {
    switch(value) {
        case http::tls_version::tls1_0:
#ifdef CURL_SSLVERSION_MAX_TLSv1_0
            return CURL_SSLVERSION_MAX_TLSv1_0;
#else
            return 0;
#endif
        case http::tls_version::tls1_1:
#ifdef CURL_SSLVERSION_MAX_TLSv1_1
            return CURL_SSLVERSION_MAX_TLSv1_1;
#else
            return 0;
#endif
        case http::tls_version::tls1_2:
#ifdef CURL_SSLVERSION_MAX_TLSv1_2
            return CURL_SSLVERSION_MAX_TLSv1_2;
#else
            return 0;
#endif
        case http::tls_version::tls1_3:
#ifdef CURL_SSLVERSION_MAX_TLSv1_3
            return CURL_SSLVERSION_MAX_TLSv1_3;
#else
            return 0;
#endif
    }

    return 0;
}

inflight_request::inflight_request(http::request request) noexcept :
    request_settings(std::move(request)), shared(std::move(request.shared)),
    method_name(std::move(request.method_name)), url_string(std::move(request.url_string)),
    query_params(std::move(request.query_params)), body_text(std::move(request.body_text)) {
    easy = curl::easy_handle::create();
    if(!easy) {
        fail(CURLE_FAILED_INIT);
        return;
    }

    if(!easy_setopt(*this, CURLOPT_WRITEFUNCTION, &inflight_request::on_write) ||
       !easy_setopt(*this, CURLOPT_WRITEDATA, this) ||
       !easy_setopt(*this, CURLOPT_HEADERFUNCTION, &inflight_request::on_header) ||
       !easy_setopt(*this, CURLOPT_HEADERDATA, this)) {
        return;
    }
}

std::size_t
    inflight_request::on_write(char* data, std::size_t size, std::size_t count, void* userdata) {
    auto* self = static_cast<inflight_request*>(userdata);
    assert(self != nullptr && "curl write callback requires inflight_request");

    const auto bytes = size * count;
    auto* begin = reinterpret_cast<const std::byte*>(data);
    self->out.body.insert(self->out.body.end(), begin, begin + bytes);
    return bytes;
}

std::size_t
    inflight_request::on_header(char* data, std::size_t size, std::size_t count, void* userdata) {
    auto* self = static_cast<inflight_request*>(userdata);
    assert(self != nullptr && "curl header callback requires inflight_request");

    const auto bytes = size * count;
    std::string_view line(data, bytes);
    while(line.ends_with('\n') || line.ends_with('\r')) {
        line.remove_suffix(1);
    }

    if(line.empty()) {
        return bytes;
    }

    if(line.starts_with("HTTP/")) {
        self->out.headers.clear();
        return bytes;
    }

    const auto colon = line.find(':');
    if(colon == std::string_view::npos) {
        return bytes;
    }

    auto name = detail::trim_ascii(line.substr(0, colon));
    auto value = detail::trim_ascii(line.substr(colon + 1));
    self->out.headers.push_back({name, value});
    return bytes;
}

bool inflight_request::fail(error err) noexcept {
    result = std::move(err);
    return false;
}

bool inflight_request::fail(curl::easy_error code) noexcept {
    return fail(error::from_curl(code));
}

bool inflight_request::apply_url() noexcept {
    if(url_string.empty()) {
        return fail(error::invalid_request("request url must not be empty"));
    }

    final_url = url_string;
    if(!query_params.empty()) {
        final_url += final_url.find('?') != std::string::npos ? '&' : '?';
        final_url += detail::encode_pairs(query_params);
    }

    return easy_setopt(*this, CURLOPT_URL, final_url.c_str());
}

bool inflight_request::apply_method() noexcept {
    if(method_name.empty()) {
        return fail(error::invalid_request("request method must not be empty"));
    }

    if(detail::iequals(method_name, http::method::get)) {
        return true;
    }

    if(detail::iequals(method_name, http::method::post)) {
        return easy_setopt(*this, CURLOPT_POST, 1L);
    }

    if(detail::iequals(method_name, http::method::head)) {
        return easy_setopt(*this, CURLOPT_NOBODY, 1L) &&
               easy_setopt(*this, CURLOPT_CUSTOMREQUEST, method_name.c_str());
    }

    return easy_setopt(*this, CURLOPT_CUSTOMREQUEST, method_name.c_str());
}

bool inflight_request::apply_body() noexcept {
    if(body_text.empty()) {
        return true;
    }

    if(detail::iequals(method_name, http::method::get) ||
       detail::iequals(method_name, http::method::head)) {
        return fail(error::invalid_request("request body is not supported for GET or HEAD"));
    }

    return easy_setopt(*this,
                       CURLOPT_POSTFIELDSIZE_LARGE,
                       static_cast<curl_off_t>(body_text.size())) &&
           easy_setopt(*this, CURLOPT_COPYPOSTFIELDS, body_text.c_str());
}

bool inflight_request::apply_headers() noexcept {
    for(const auto& item: header_list) {
        std::string line = item.name;
        line += ": ";
        line += item.value;
        if(!header_lines.append(line.c_str())) {
            return fail(CURLE_OUT_OF_MEMORY);
        }
    }

    if(!header_lines) {
        return true;
    }

    return easy_setopt(*this, CURLOPT_HTTPHEADER, header_lines.get());
}

bool inflight_request::apply_cookies() noexcept {
    if(cookie_string.empty()) {
        return true;
    }

    return easy_setopt(*this, CURLOPT_COOKIE, cookie_string.c_str());
}

bool inflight_request::apply_user_agent() noexcept {
    if(user_agent_value.empty()) {
        return true;
    }

    return easy_setopt(*this, CURLOPT_USERAGENT, user_agent_value.c_str());
}

bool inflight_request::apply_redirect() noexcept {
    if(!redirect_policy_value.follow) {
        return easy_setopt(*this, CURLOPT_FOLLOWLOCATION, 0L);
    }

    return easy_setopt(*this, CURLOPT_FOLLOWLOCATION, 1L) &&
           easy_setopt(*this,
                       CURLOPT_MAXREDIRS,
                       static_cast<long>(redirect_policy_value.max_redirects)) &&
           easy_setopt(*this, CURLOPT_AUTOREFERER, redirect_policy_value.referer ? 1L : 0L);
}

bool inflight_request::apply_tls() noexcept {
    if(tls_config.min_version && tls_config.max_version &&
       tls_version_rank(*tls_config.min_version) > tls_version_rank(*tls_config.max_version)) {
        return fail(error::invalid_request("min tls version must not exceed max tls version"));
    }

#if LIBCURL_VERSION_NUM >= 0x075500
    const char* protocols = tls_config.https_only ? "https" : "http,https";
    if(!easy_setopt(*this, CURLOPT_PROTOCOLS_STR, protocols) ||
       !easy_setopt(*this, CURLOPT_REDIR_PROTOCOLS_STR, protocols)) {
        return false;
    }
#else
    long protocols = tls_config.https_only ? CURLPROTO_HTTPS
                                           : static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS);
    if(!easy_setopt(*this, CURLOPT_PROTOCOLS, protocols) ||
       !easy_setopt(*this, CURLOPT_REDIR_PROTOCOLS, protocols)) {
        return false;
    }
#endif

    if(!easy_setopt(*this,
                    CURLOPT_SSL_VERIFYPEER,
                    tls_config.danger_accept_invalid_certs ? 0L : 1L) ||
       !easy_setopt(*this,
                    CURLOPT_SSL_VERIFYHOST,
                    tls_config.danger_accept_invalid_hostnames ? 0L : 2L)) {
        return false;
    }

    if(tls_config.ca_file && !easy_setopt(*this, CURLOPT_CAINFO, tls_config.ca_file->c_str())) {
        return false;
    }

    if(tls_config.ca_path && !easy_setopt(*this, CURLOPT_CAPATH, tls_config.ca_path->c_str())) {
        return false;
    }

    if(tls_config.min_version || tls_config.max_version) {
        long version = CURL_SSLVERSION_DEFAULT;
        if(tls_config.min_version) {
            version = to_curl_ssl_min(*tls_config.min_version);
        }
        if(tls_config.max_version) {
            auto upper = to_curl_ssl_max(*tls_config.max_version);
            if(upper == 0) {
                return fail(error::invalid_request(
                    "libcurl does not support the requested max tls version"));
            }
            version |= upper;
        }
        if(!easy_setopt(*this, CURLOPT_SSLVERSION, version)) {
            return false;
        }
    }

    return true;
}

bool inflight_request::apply_proxy() noexcept {
    if(disable_proxy) {
        return easy_setopt(*this, CURLOPT_PROXY, "");
    }

    if(!proxy_config) {
        return true;
    }

    const auto& proxy = *proxy_config;
    if(proxy.url.empty()) {
        return fail(error::invalid_request("proxy url must not be empty"));
    }

    if(!easy_setopt(*this, CURLOPT_PROXY, proxy.url.c_str())) {
        return false;
    }

    if(!proxy.username.empty() &&
       !easy_setopt(*this, CURLOPT_PROXYUSERNAME, proxy.username.c_str())) {
        return false;
    }

    if(!proxy.password.empty() &&
       !easy_setopt(*this, CURLOPT_PROXYPASSWORD, proxy.password.c_str())) {
        return false;
    }

    return true;
}

bool inflight_request::apply_timeout() noexcept {
    if(!timeout_value) {
        return true;
    }

    const auto timeout_ms = timeout_value->count();
    if(timeout_ms < 0) {
        return fail(error::invalid_request("timeout must be non-negative"));
    }

    if(timeout_ms > (std::numeric_limits<long>::max)()) {
        return fail(error::invalid_request("timeout exceeds libcurl timeout range"));
    }

    return easy_setopt(*this, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout_ms));
}

bool inflight_request::apply_curl_options() noexcept {
    for(const auto& option: curl_options) {
        if(auto err = option(easy.get()); !curl::ok(err)) {
            return fail(err);
        }
    }

    return true;
}

bool inflight_request::prepare() noexcept {
    if(!easy || result.kind != error_kind::curl || !curl::ok(result.curl_code)) {
        return false;
    }

    if(!bind_easy(easy.get(), shared, record_cookie_enabled)) {
        return fail(error::invalid_request("failed to bind curl easy to shared request resources"));
    }

    if(!apply_url() || !apply_method() || !apply_body() || !apply_headers() || !apply_cookies() ||
       !apply_user_agent() || !apply_redirect() || !apply_tls() || !apply_proxy() ||
       !apply_timeout() || !apply_curl_options()) {
        return false;
    }

    return true;
}

bool inflight_request::bind_runtime(void* opaque) noexcept {
    if(!easy || result.kind != error_kind::curl || !curl::ok(result.curl_code)) {
        return false;
    }

    return easy_setopt(*this, CURLOPT_PRIVATE, opaque);
}

void inflight_request::clear_runtime_binding() noexcept {
    if(!easy) {
        return;
    }

    [[maybe_unused]] auto err = curl::setopt(easy.get(), CURLOPT_PRIVATE, nullptr);
}

outcome<response, error, cancellation> inflight_request::finish() noexcept {
    if(result.kind != error_kind::curl || !curl::ok(result.curl_code)) {
        return outcome<response, error, cancellation>(outcome_error(std::move(result)));
    }

    long status = 0;
    if(auto err = curl::getinfo(easy.get(), CURLINFO_RESPONSE_CODE, &status); !curl::ok(err)) {
        return outcome<response, error, cancellation>(outcome_error(error::from_curl(err)));
    }
    out.status = status;

    char* effective = nullptr;
    if(auto err = curl::getinfo(easy.get(), CURLINFO_EFFECTIVE_URL, &effective);
       curl::ok(err) && effective != nullptr) {
        out.url = effective;
    } else {
        out.url = final_url;
    }

    easy.reset();
    return outcome<response, error, cancellation>(std::move(out));
}

}  // namespace detail

}  // namespace kota::http
