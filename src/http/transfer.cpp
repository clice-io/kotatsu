#include "transfer.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "kota/http/detail/util.h"

namespace kota::http::detail {

namespace {

/// Whether `text` is an HTTP token, as method and header names are.
bool is_token(std::string_view text) {
    constexpr std::string_view marks = "!#$%&'*+-.^_`|~";
    return !text.empty() && std::ranges::all_of(text, [&](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               marks.find(c) != std::string_view::npos;
    });
}

/// Whether `text` stays on the header line it goes into: CR or LF would end
/// the line and start one of its own, and curl stops at NUL.
bool fits_line(std::string_view text) {
    return text.find_first_of(std::string_view("\r\n\0", 3)) == std::string_view::npos;
}

long ssl_min(http::tls_version version) {
    switch(version) {
        case http::tls_version::tls1_0: return CURL_SSLVERSION_TLSv1_0;
        case http::tls_version::tls1_1: return CURL_SSLVERSION_TLSv1_1;
        case http::tls_version::tls1_2: return CURL_SSLVERSION_TLSv1_2;
        case http::tls_version::tls1_3: return CURL_SSLVERSION_TLSv1_3;
    }
    std::unreachable();
}

long ssl_max(http::tls_version version) {
    switch(version) {
        case http::tls_version::tls1_0: return CURL_SSLVERSION_MAX_TLSv1_0;
        case http::tls_version::tls1_1: return CURL_SSLVERSION_MAX_TLSv1_1;
        case http::tls_version::tls1_2: return CURL_SSLVERSION_MAX_TLSv1_2;
        case http::tls_version::tls1_3: return CURL_SSLVERSION_MAX_TLSv1_3;
    }
    std::unreachable();
}

}  // namespace

transfer::transfer(http::request request, manager& target) noexcept :
    request(std::move(request)), owner(&target) {}

std::optional<error> transfer::check(const http::request& request) {
    if(request.url_string.empty()) {
        return error::invalid_request("request url must not be empty");
    }
    if(!is_token(request.method_name)) {
        return error::invalid_request("request method must be an http token");
    }
    if(!request.body_text.empty() && (iequals(request.method_name, http::method::get) ||
                                      iequals(request.method_name, http::method::head))) {
        return error::invalid_request("request body is not supported for GET or HEAD");
    }
    for(const auto& [name, value]: request.header_list) {
        if(!is_token(name) || !fits_line(value)) {
            return error::invalid_request(
                "header names must be http tokens, and values must not hold CR, LF or NUL");
        }
    }
    if(!fits_line(request.cookie_string) || !fits_line(request.user_agent_value)) {
        return error::invalid_request("cookies and user agent must not hold CR, LF or NUL");
    }
    const auto& tls = request.tls_config;
    if(tls.min_version && tls.max_version && *tls.min_version > *tls.max_version) {
        return error::invalid_request("min tls version must not exceed max tls version");
    }
    if(request.proxy_config && request.proxy_config->url.empty()) {
        return error::invalid_request("proxy url must not be empty");
    }
    if(request.timeout_value) {
        const auto ms = request.timeout_value->count();
        if(ms < 0) {
            return error::invalid_request("timeout must be non-negative");
        }
        // libcurl takes a long, which has 32 bits on Windows.
        if(ms > (std::numeric_limits<long>::max)()) {
            return error::invalid_request("timeout exceeds libcurl timeout range");
        }
    }
    return std::nullopt;
}

task<response, error> transfer::send(http::request request) {
    if(request.staged_error) {
        co_await fail(std::move(*request.staged_error));
    }
    if(auto invalid = check(request)) {
        co_await fail(std::move(*invalid));
    }
    auto target = manager::try_for_loop(*request.dispatch_loop);
    if(!target) {
        co_await fail(std::move(target).error());
    }

    transfer job(std::move(request), target->get());
    auto result = co_await job;
    if(result.has_error()) {
        co_await fail(std::move(result).error());
    }
    co_return std::move(*result);
}

bool transfer::start() noexcept {
    if(auto err = setup()) {
        failure = std::move(err);
        owner = nullptr;
        return false;
    }
    if(auto err = owner->add(*this); !curl::ok(err)) {
        failure = error::from_curl(err, std::string(curl::message(err)));
        owner = nullptr;
        return false;
    }
    return true;
}

void transfer::cancel() noexcept {
    if(owner != nullptr) {
        owner->drop(*this);
    }
    // A queued completion ends it otherwise.
    if(!queued) {
        complete();
    }
}

outcome<response, error> transfer::await_resume() noexcept {
    if(owner != nullptr) {
        owner->drop(*this);
    }
    if(failure) {
        return outcome_error(std::move(*failure));
    }

    long status = 0;
    char* url = nullptr;
    curl::getinfo(easy.get(), CURLINFO_RESPONSE_CODE, &status);
    curl::getinfo(easy.get(), CURLINFO_EFFECTIVE_URL, &url);
    out.status = static_cast<int>(status);
    out.url = url;
    return std::move(out);
}

std::optional<error> transfer::setup() {
    easy = curl::easy_handle::create();
    if(!easy) {
        return error::from_curl(CURLE_FAILED_INIT, "failed to create curl easy handle");
    }
    auto jar = owner->share_for(request.key);
    if(!jar) {
        return std::move(jar).error();
    }
    share = std::move(*jar);

    // The first option that fails stops the rest.
    auto code = CURLE_OK;
    auto set = [&](CURLoption option, auto value) {
        if(code == CURLE_OK) {
            code = curl::setopt(easy.get(), option, value);
        }
    };

    set(CURLOPT_WRITEFUNCTION, static_cast<curl_write_callback>(on_write));
    set(CURLOPT_WRITEDATA, static_cast<void*>(this));
    set(CURLOPT_HEADERFUNCTION, static_cast<curl_write_callback>(on_header));
    set(CURLOPT_HEADERDATA, static_cast<void*>(this));
    // curl's own reader reads the process's stdin when an upload a
    // curl_option() asks for comes without a CURLOPT_READDATA.
    set(CURLOPT_READFUNCTION, static_cast<curl_read_callback>(on_read));
    set(CURLOPT_READDATA, static_cast<void*>(nullptr));
    set(CURLOPT_SHARE, share->get());
    if(request.record_cookie_enabled) {
        set(CURLOPT_COOKIEFILE, "");
    }

    auto url = request.url_string;
    if(!request.query_params.empty()) {
        url += url.find('?') == std::string::npos ? '?' : '&';
        url += encode_pairs(request.query_params);
    }
    set(CURLOPT_URL, url.c_str());

    const auto& method = request.method_name;
    const bool post = iequals(method, http::method::post);
    if(iequals(method, http::method::head)) {
        set(CURLOPT_NOBODY, 1L);
    } else if(!post && !iequals(method, http::method::get)) {
        set(CURLOPT_CUSTOMREQUEST, method.c_str());
    }
    if(!request.body_text.empty()) {
        set(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body_text.size()));
        set(CURLOPT_POSTFIELDS, request.body_text.c_str());
    } else if(post) {
        // Without a body, a POST would read one with the reader above,
        // chunked. curl takes this one's length with strlen, as it does that
        // of a CURLOPT_POSTFIELDS a curl_option() sets instead.
        set(CURLOPT_POSTFIELDS, "");
    }

    for(const auto& [name, value]: request.header_list) {
        // "Name;" sends a header with no value; "Name:" would remove it.
        auto line = value.empty() ? std::format("{};", name) : std::format("{}: {}", name, value);
        if(!header_lines.append(line.c_str())) {
            return error::from_curl(CURLE_OUT_OF_MEMORY);
        }
    }
    if(header_lines) {
        set(CURLOPT_HTTPHEADER, header_lines.get());
    }
    if(!request.cookie_string.empty()) {
        set(CURLOPT_COOKIE, request.cookie_string.c_str());
    }
    if(!request.user_agent_value.empty()) {
        set(CURLOPT_USERAGENT, request.user_agent_value.c_str());
    }

    const auto& redirect = request.redirect_policy_value;
    set(CURLOPT_FOLLOWLOCATION, redirect.follow ? 1L : 0L);
    if(redirect.follow) {
        set(CURLOPT_MAXREDIRS, static_cast<long>(redirect.max_redirects));
        set(CURLOPT_AUTOREFERER, redirect.referer ? 1L : 0L);
    }

    const auto& tls = request.tls_config;
    const char* protocols = tls.https_only ? "https" : "http,https";
    set(CURLOPT_PROTOCOLS_STR, protocols);
    set(CURLOPT_REDIR_PROTOCOLS_STR, protocols);
    set(CURLOPT_SSL_VERIFYPEER, tls.danger_accept_invalid_certs ? 0L : 1L);
    set(CURLOPT_SSL_VERIFYHOST, tls.danger_accept_invalid_hostnames ? 0L : 2L);
    if(tls.ca_file) {
        set(CURLOPT_CAINFO, tls.ca_file->c_str());
    }
    if(tls.ca_path) {
        set(CURLOPT_CAPATH, tls.ca_path->c_str());
    }
    if(tls.min_version || tls.max_version) {
        set(CURLOPT_SSLVERSION,
            (tls.min_version ? ssl_min(*tls.min_version) : CURL_SSLVERSION_DEFAULT) |
                (tls.max_version ? ssl_max(*tls.max_version) : CURL_SSLVERSION_MAX_NONE));
    }

    if(request.disable_proxy) {
        set(CURLOPT_PROXY, "");
    } else if(request.proxy_config) {
        const auto& proxy = *request.proxy_config;
        set(CURLOPT_PROXY, proxy.url.c_str());
        if(!proxy.username.empty()) {
            set(CURLOPT_PROXYUSERNAME, proxy.username.c_str());
        }
        if(!proxy.password.empty()) {
            set(CURLOPT_PROXYPASSWORD, proxy.password.c_str());
        }
    }

    if(request.timeout_value) {
        set(CURLOPT_TIMEOUT_MS, static_cast<long>(request.timeout_value->count()));
    }

    for(const auto& option: request.curl_options) {
        if(code == CURLE_OK) {
            code = option(easy.get());
        }
    }
    // After the caller's options, which must not replace it: how the
    // manager finds the transfer of a handle curl has finished.
    set(CURLOPT_PRIVATE, static_cast<void*>(this));

    if(code != CURLE_OK) {
        return error::from_curl(code);
    }
    return std::nullopt;
}

std::size_t transfer::on_write(char* data, std::size_t size, std::size_t count, void* self) {
    auto& body = static_cast<transfer*>(self)->out.body;
    const auto* begin = reinterpret_cast<const std::byte*>(data);
    body.insert(body.end(), begin, begin + size * count);
    return size * count;
}

std::size_t transfer::on_header(char* data, std::size_t size, std::size_t count, void* self) {
    auto& headers = static_cast<transfer*>(self)->out.headers;
    std::string_view line(data, size * count);
    // A status line starts each response curl reads, the interim ones and
    // those redirects end with included: keep the last response's headers.
    if(line.starts_with("HTTP/")) {
        headers.clear();
    } else if(auto colon = line.find(':'); colon != std::string_view::npos) {
        headers.push_back({trim_ascii(line.substr(0, colon)), trim_ascii(line.substr(colon + 1))});
    }
    return size * count;
}

std::size_t transfer::on_read(char* data, std::size_t size, std::size_t count, void* file) {
    // curl's own reader, but one that reads nothing when no file was given.
    if(file == nullptr) {
        return 0;
    }
    return std::fread(data, size, count, static_cast<std::FILE*>(file));
}

}  // namespace kota::http::detail
