#include "kota/http/detail/request.h"

#include <format>
#include <utility>

#include "transfer.h"
#include "kota/http/detail/util.h"

namespace kota::http {

request::request(const detail::request_settings& settings,
                 std::shared_ptr<const detail::share_key> key,
                 event_loop& loop,
                 std::string method,
                 std::string url) noexcept :
    detail::request_settings(settings), key(std::move(key)), dispatch_loop(&loop),
    method_name(std::move(method)), url_string(std::move(url)) {}

request& request::query(std::string name, std::string value) {
    query_params.push_back({std::move(name), std::move(value)});
    return *this;
}

request& request::method(std::string value) {
    method_name = std::move(value);
    return *this;
}

request& request::bearer_auth(std::string token) {
    return header("authorization", std::format("Bearer {}", token));
}

request& request::basic_auth(std::string username, std::string password) {
    return header(
        "authorization",
        std::format("Basic {}", detail::base64_encode(std::format("{}:{}", username, password))));
}

request& request::json_text(std::string body) {
    body_text = std::move(body);
    return header("content-type", "application/json");
}

request& request::form(std::vector<query_param> fields) {
    body_text = detail::encode_pairs(fields);
    return header("content-type", "application/x-www-form-urlencoded");
}

request& request::body(std::string body) {
    body_text = std::move(body);
    return *this;
}

task<response, error> request::send() & {
    return detail::transfer::send(*this);
}

task<response, error> request::send() && {
    return detail::transfer::send(std::move(*this));
}

void request::remember_error(error err) noexcept {
    if(!staged_error) {
        staged_error = std::move(err);
    }
}

}  // namespace kota::http
