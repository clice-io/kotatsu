#include "kota/http/detail/request.h"

#include <format>
#include <utility>

namespace kota::http {

namespace {

task<response, error> make_failed_response(error err) {
    co_await fail(std::move(err));
}

}  // namespace

request::request(std::shared_ptr<detail::shared_resources> shared,
                 event_loop* dispatch_loop) noexcept :
    shared(std::move(shared)), dispatch_loop(dispatch_loop) {}

request::request(const detail::request_settings& settings,
                 std::shared_ptr<detail::shared_resources> shared,
                 event_loop* dispatch_loop) noexcept :
    detail::request_settings(settings), shared(std::move(shared)), dispatch_loop(dispatch_loop) {}

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
    header("content-type", "application/json");
    return *this;
}

request& request::form(std::vector<query_param> fields) {
    body_text = detail::encode_pairs(fields);
    header("content-type", "application/x-www-form-urlencoded");
    return *this;
}

request& request::body(std::string body) {
    body_text = std::move(body);
    return *this;
}

task<response, error> request::send() & {
    if(staged_error) {
        return failed(*staged_error);
    }

    if(!dispatch_loop) {
        return failed(error::invalid_request("request::send requires a bound_client"));
    }

    return detail::execute_request(*this, *dispatch_loop);
}

task<response, error> request::send() && {
    if(staged_error) {
        return failed(std::move(*staged_error));
    }

    if(!dispatch_loop) {
        return failed(error::invalid_request("request::send requires a bound_client"));
    }

    return detail::execute_request(std::move(*this), *dispatch_loop);
}

task<response, error> request::failed(error err) {
    return make_failed_response(std::move(err));
}

void request::remember_error(error err) noexcept {
    if(!staged_error) {
        staged_error = std::move(err);
    }
}

}  // namespace kota::http
