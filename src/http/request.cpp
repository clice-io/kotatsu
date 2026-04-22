#include <cassert>
#include <chrono>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "kota/http/detail/client.h"
#include "kota/http/detail/client_state.h"
#include "kota/http/detail/runtime.h"
#include "kota/http/detail/util.h"

namespace kota::http {

namespace {

request make_request(std::string method, std::string url, const client_options& options) {
    request req;
    req.method = std::move(method);
    req.url = std::move(url);
    req.headers = options.default_headers;
    req.cookie = options.default_cookies;
    req.user_agent = options.user_agent;
    req.proxy_config = options.proxy_config;
    req.redirect = options.redirect;
    req.tls = options.tls;
    req.timeout = options.timeout;
    req.curl_options = options.curl_options;
    req.record_cookie = options.record_cookie;
    req.disable_proxy = options.disable_proxy;
    return req;
}

task<response, error> make_failed_response(error err) {
    co_await fail(std::move(err));
}

request_builder make_request_builder(const std::shared_ptr<client_state>& state,
                                     event_loop* dispatch_loop,
                                     std::string method,
                                     std::string url) {
    return request_builder(state,
                           dispatch_loop,
                           make_request(std::move(method), std::move(url), state->options()));
}

template <typename Options>
std::expected<client, error> build_client(event_loop* bound_loop, Options&& options) {
    if(auto code = detail::ensure_curl_runtime(); !curl::ok(code)) {
        return std::unexpected(error::from_curl(code));
    }

    client out(std::forward<Options>(options));
    if(bound_loop) {
        out.bind(*bound_loop);
    }
    return out;
}

void require_share_setopt(CURLSH* share,
                          CURLSHoption option,
                          auto value,
                          [[maybe_unused]] const char* message) noexcept {
    [[maybe_unused]] auto err = curl::share_setopt(share, option, value);
    assert(curl::ok(err) && message);
}

}  // namespace

client_state::client_state(client_options opts) : defaults(std::move(opts)) {
    if(auto code = detail::ensure_curl_runtime(); !curl::ok(code)) {
        std::abort();
    }

    share = curl::share_handle::create();
    if(!share) {
        std::abort();
    }

    require_share_setopt(share.get(),
                         CURLSHOPT_LOCKFUNC,
                         &client_state::on_share_lock,
                         "curl share lock registration failed");
    require_share_setopt(share.get(),
                         CURLSHOPT_UNLOCKFUNC,
                         &client_state::on_share_unlock,
                         "curl share unlock registration failed");
    require_share_setopt(share.get(),
                         CURLSHOPT_USERDATA,
                         static_cast<void*>(this),
                         "curl share userdata registration failed");
    require_share_setopt(share.get(),
                         CURLSHOPT_SHARE,
                         CURL_LOCK_DATA_COOKIE,
                         "curl share cookie registration failed");
    require_share_setopt(share.get(),
                         CURLSHOPT_SHARE,
                         CURL_LOCK_DATA_DNS,
                         "curl share dns registration failed");
    require_share_setopt(share.get(),
                         CURLSHOPT_SHARE,
                         CURL_LOCK_DATA_SSL_SESSION,
                         "curl share ssl session registration failed");
}

void client_state::bind(event_loop& loop) noexcept {
    bound_loop = &loop;
}

bool client_state::is_bound() const noexcept {
    return bound_loop != nullptr;
}

std::optional<std::reference_wrapper<event_loop>> client_state::loop() const noexcept {
    if(!bound_loop) {
        return std::nullopt;
    }

    return *bound_loop;
}

const client_options& client_state::options() const noexcept {
    return defaults;
}

void client_state::record_cookie(bool enabled) noexcept {
    defaults.record_cookie = enabled;
}

bool client_state::bind_easy(CURL* easy, bool enable_record_cookie) const noexcept {
    if(!easy || !share) {
        return false;
    }

    if(auto err = curl::setopt(easy, CURLOPT_SHARE, share.get()); !curl::ok(err)) {
        return false;
    }

    if(enable_record_cookie) {
        if(auto err = curl::setopt(easy, CURLOPT_COOKIEFILE, ""); !curl::ok(err)) {
            return false;
        }
    }

    return true;
}

void client_state::on_share_lock(CURL*,
                                 curl_lock_data data,
                                 curl_lock_access,
                                 void* userptr) noexcept {
    auto* self = static_cast<client_state*>(userptr);
    if(!self) {
        return;
    }
    self->mutex_for(data).lock();
}

void client_state::on_share_unlock(CURL*, curl_lock_data data, void* userptr) noexcept {
    auto* self = static_cast<client_state*>(userptr);
    if(!self) {
        return;
    }
    self->mutex_for(data).unlock();
}

std::mutex& client_state::mutex_for(curl_lock_data data) noexcept {
    switch(data) {
        case CURL_LOCK_DATA_COOKIE: return cookie_mu;
        case CURL_LOCK_DATA_DNS: return dns_mu;
        case CURL_LOCK_DATA_SSL_SESSION: return ssl_session_mu;
        default: return admin_mu;
    }
}

request_builder::request_builder(std::shared_ptr<client_state> owner,
                                 event_loop* dispatch_loop,
                                 request req) noexcept :
    owner(std::move(owner)), dispatch_loop(dispatch_loop), spec(std::move(req)) {}

request_builder& request_builder::header(std::string name, std::string value) {
    detail::upsert_header(spec.headers, std::move(name), std::move(value));
    return *this;
}

request_builder& request_builder::query(std::string name, std::string value) {
    spec.query.push_back({std::move(name), std::move(value)});
    return *this;
}

request_builder& request_builder::cookies(std::string value) {
    spec.cookie = std::move(value);
    return *this;
}

request_builder& request_builder::method(std::string value) {
    spec.method = std::move(value);
    return *this;
}

request_builder& request_builder::bearer_auth(std::string token) {
    return header("authorization", std::format("Bearer {}", token));
}

request_builder& request_builder::basic_auth(std::string username, std::string password) {
    return header(
        "authorization",
        std::format("Basic {}", detail::base64_encode(std::format("{}:{}", username, password))));
}

request_builder& request_builder::user_agent(std::string value) {
    spec.user_agent = std::move(value);
    return *this;
}

request_builder& request_builder::proxy(http::proxy value) {
    spec.proxy_config = std::move(value);
    spec.disable_proxy = false;
    return *this;
}

request_builder& request_builder::proxy(std::string url) {
    return proxy(http::proxy{.url = std::move(url), .username = {}, .password = {}});
}

request_builder& request_builder::no_proxy() {
    spec.proxy_config.reset();
    spec.disable_proxy = true;
    return *this;
}

request_builder& request_builder::timeout(std::chrono::milliseconds value) {
    spec.timeout = value;
    return *this;
}

request_builder& request_builder::json_text(std::string body) {
    spec.body = std::move(body);
    detail::upsert_header(spec.headers, "content-type", "application/json");
    return *this;
}

request_builder& request_builder::form(std::vector<query_param> fields) {
    spec.body = detail::encode_pairs(fields);
    detail::upsert_header(spec.headers, "content-type", "application/x-www-form-urlencoded");
    return *this;
}

request_builder& request_builder::body(std::string body) {
    spec.body = std::move(body);
    return *this;
}

task<response, error> request_builder::failed(error err) {
    return make_failed_response(std::move(err));
}

void request_builder::remember_error(error err) noexcept {
    if(!staged_error) {
        staged_error = std::move(err);
    }
}

std::optional<std::reference_wrapper<event_loop>>
    request_builder::resolve_loop(const std::shared_ptr<client_state>& owner,
                                  event_loop* dispatch_loop) noexcept {
    if(dispatch_loop) {
        return *dispatch_loop;
    }

    if(!owner) {
        return std::nullopt;
    }

    return owner->loop();
}

client_builder::client_builder(event_loop& loop) noexcept : bound_loop(&loop) {}

client_builder& client_builder::bind(event_loop& loop) noexcept {
    bound_loop = &loop;
    return *this;
}

client_builder& client_builder::default_header(std::string name, std::string value) {
    detail::upsert_header(options.default_headers, std::move(name), std::move(value));
    return *this;
}

client_builder& client_builder::user_agent(std::string value) {
    options.user_agent = std::move(value);
    return *this;
}

client_builder& client_builder::proxy(http::proxy value) {
    options.proxy_config = std::move(value);
    options.disable_proxy = false;
    return *this;
}

client_builder& client_builder::proxy(std::string url) {
    return proxy(http::proxy{.url = std::move(url), .username = {}, .password = {}});
}

client_builder& client_builder::no_proxy() {
    options.proxy_config.reset();
    options.disable_proxy = true;
    return *this;
}

client_builder& client_builder::timeout(std::chrono::milliseconds value) {
    options.timeout = value;
    return *this;
}

client_builder& client_builder::redirect(redirect_policy value) {
    options.redirect = value;
    return *this;
}

client_builder& client_builder::referer(bool enabled) {
    options.redirect.referer = enabled;
    return *this;
}

client_builder& client_builder::https_only(bool enabled) {
    options.tls.https_only = enabled;
    return *this;
}

client_builder& client_builder::danger_accept_invalid_certs(bool enabled) {
    options.tls.danger_accept_invalid_certs = enabled;
    return *this;
}

client_builder& client_builder::danger_accept_invalid_hostnames(bool enabled) {
    options.tls.danger_accept_invalid_hostnames = enabled;
    return *this;
}

client_builder& client_builder::min_tls_version(http::tls_version value) {
    options.tls.min_version = value;
    return *this;
}

client_builder& client_builder::max_tls_version(http::tls_version value) {
    options.tls.max_version = value;
    return *this;
}

client_builder& client_builder::ca_file(std::string path) {
    options.tls.ca_file = std::move(path);
    return *this;
}

client_builder& client_builder::ca_path(std::string path) {
    options.tls.ca_path = std::move(path);
    return *this;
}

std::expected<client, error> client_builder::build() const& {
    return build_client(bound_loop, options);
}

std::expected<client, error> client_builder::build() && {
    return build_client(bound_loop, std::move(options));
}

client::client(client_options options) :
    state(std::make_shared<client_state>(std::move(options))) {}

client::client(event_loop& loop, client_options options) : client(std::move(options)) {
    bind(loop);
}

client::~client() = default;

client_builder client::builder() noexcept {
    return client_builder();
}

client_builder client::builder(event_loop& loop) noexcept {
    return client_builder(loop);
}

client::client(client&&) noexcept = default;

client& client::operator=(client&&) noexcept = default;

client& client::bind(event_loop& loop) noexcept {
    state->bind(loop);
    return *this;
}

bool client::is_bound() const noexcept {
    return state->is_bound();
}

bound_client client::on(event_loop& loop) & noexcept {
    return bound_client(state, loop);
}

request_builder client::request(std::string method, std::string url) const& {
    return make_request_builder(state, nullptr, std::move(method), std::move(url));
}

request_builder client::get(std::string url) const& {
    return request(std::string(http::method::get), std::move(url));
}

request_builder client::post(std::string url) const& {
    return request(std::string(http::method::post), std::move(url));
}

request_builder client::put(std::string url) const& {
    return request(std::string(http::method::put), std::move(url));
}

request_builder client::patch(std::string url) const& {
    return request(std::string(http::method::patch), std::move(url));
}

request_builder client::del(std::string url) const& {
    return request(std::string(http::method::del), std::move(url));
}

request_builder client::head(std::string url) const& {
    return request(std::string(http::method::head), std::move(url));
}

task<response, error> client::execute(http::request req) const& {
    auto bound = loop();
    if(!bound) {
        return make_failed_response(
            error::invalid_request("client::execute requires a bound loop"));
    }
    return detail::execute_with_state(std::move(req), bound->get(), state);
}

client& client::record_cookie(bool enabled) noexcept {
    state->record_cookie(enabled);
    return *this;
}

std::optional<std::reference_wrapper<event_loop>> client::loop() const noexcept {
    return state->loop();
}

const client_options& client::options() const noexcept {
    return state->options();
}

request_builder bound_client::request(std::string method, std::string url) const noexcept {
    return make_request_builder(state, dispatch_loop, std::move(method), std::move(url));
}

request_builder bound_client::get(std::string url) const noexcept {
    return request(std::string(http::method::get), std::move(url));
}

request_builder bound_client::post(std::string url) const noexcept {
    return request(std::string(http::method::post), std::move(url));
}

request_builder bound_client::put(std::string url) const noexcept {
    return request(std::string(http::method::put), std::move(url));
}

request_builder bound_client::patch(std::string url) const noexcept {
    return request(std::string(http::method::patch), std::move(url));
}

request_builder bound_client::del(std::string url) const noexcept {
    return request(std::string(http::method::del), std::move(url));
}

request_builder bound_client::head(std::string url) const noexcept {
    return request(std::string(http::method::head), std::move(url));
}

task<response, error> bound_client::execute(http::request req) const {
    if(!state || !dispatch_loop) {
        return make_failed_response(error::invalid_request("bound_client requires a valid loop"));
    }

    return detail::execute_with_state(std::move(req), *dispatch_loop, state);
}

}  // namespace kota::http
