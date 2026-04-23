#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "kota/http/detail/request_settings.h"
#include "kota/http/detail/response.h"
#include "kota/async/io/loop.h"
#include "kota/async/runtime/task.h"
#include "kota/async/vocab/error.h"

#if __has_include(<simdjson.h>)
#include "kota/codec/json/json.h"
#define KOTA_HTTP_HAS_CODEC_JSON 1
#else
#define KOTA_HTTP_HAS_CODEC_JSON 0
#endif

namespace kota::http::detail {

struct shared_resources {
    curl::share_handle share{};
};

std::shared_ptr<shared_resources> make_shared_resources();

}  // namespace kota::http::detail

namespace kota::http {

class request;
class bound_client;

}  // namespace kota::http

namespace kota::http::detail {

class inflight_request;
task<response, error> execute_request(request request, event_loop& loop);

}  // namespace kota::http::detail

namespace kota::http {

class request : public detail::request_settings {
public:
    request() = delete;

    request(std::shared_ptr<detail::shared_resources> shared, event_loop* dispatch_loop) noexcept;
    request(const detail::request_settings& settings,
            std::shared_ptr<detail::shared_resources> shared,
            event_loop* dispatch_loop) noexcept;

    request(const request&) noexcept = default;
    request& operator=(const request&) noexcept = default;
    request(request&&) noexcept = default;
    request& operator=(request&&) noexcept = default;
    ~request() = default;

    request& query(std::string name, std::string value);
    request& method(std::string value);
    request& bearer_auth(std::string token);
    request& basic_auth(std::string username, std::string password);
    request& json_text(std::string body);
    request& form(std::vector<query_param> fields);
    request& body(std::string body);

    task<response, error> send() &;
    task<response, error> send() &&;

#if KOTA_HTTP_HAS_CODEC_JSON
    template <typename T>
    request& json(const T& value) {
        auto encoded = codec::json::to_string(value);
        if(!encoded) {
            remember_error(error::json_encode(encoded.error().to_string()));
            return *this;
        }

        json_text(std::move(*encoded));
        return *this;
    }
#endif

private:
    friend class bound_client;
    friend class detail::inflight_request;
    void remember_error(error err) noexcept;
    static task<response, error> failed(error err);

    std::shared_ptr<detail::shared_resources> shared;
    event_loop* dispatch_loop = nullptr;
    std::string method_name = std::string(http::method::get);
    std::string url_string;
    std::vector<query_param> query_params;
    std::string body_text;
    std::optional<error> staged_error;
};

}  // namespace kota::http
