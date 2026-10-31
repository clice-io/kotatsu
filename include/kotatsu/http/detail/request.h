#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "kotatsu/http/detail/request_settings.h"
#include "kotatsu/http/detail/response.h"
#include "kotatsu/async/io/loop.h"
#include "kotatsu/async/runtime/task.h"

#if __has_include(<simdjson.h>)
#include "kotatsu/codec/json/json.h"
#define KOTATSU_HTTP_HAS_CODEC_JSON 1
#else
#define KOTATSU_HTTP_HAS_CODEC_JSON 0
#endif

namespace kotatsu::http::detail {

struct share_key;
struct transfer;

}  // namespace kotatsu::http::detail

namespace kotatsu::http {

class bound_client;

/// One request, built fluently and sent by send(). It keeps what it needs
/// of its client, so it may outlive the client.
class request : public detail::request_settings {
public:
    request() = delete;

    request(const request&) = default;
    request& operator=(const request&) = default;
    request(request&&) noexcept = default;
    request& operator=(request&&) noexcept = default;
    ~request() = default;

    /// Appends a query parameter to the url, percent-encoded.
    request& query(std::string name, std::string value);

    request& method(std::string value);

    request& bearer_auth(std::string token);

    request& basic_auth(std::string username, std::string password);

    /// `body` as the body, sent as application/json.
    request& json_text(std::string body);

    /// `fields` as the body, percent-encoded as
    /// application/x-www-form-urlencoded.
    request& form(std::vector<query_param> fields);

    request& body(std::string body);

    /// Sends a copy of the request once the task starts. Await the task on
    /// the loop the request's bound_client names.
    task<response, error> send() &;

    /// Sends the request once the task starts. Await the task on the loop
    /// the request's bound_client names.
    task<response, error> send() &&;

#if KOTATSU_HTTP_HAS_CODEC_JSON
    /// `value` encoded as JSON, as json_text() sends it; send() fails with
    /// error_kind::json_encode when it cannot be encoded.
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
    friend struct detail::transfer;

    request(const detail::request_settings& settings,
            std::shared_ptr<const detail::share_key> key,
            event_loop& loop,
            std::string method,
            std::string url) noexcept;

    /// Keeps the first error a builder ran into, for send() to fail with.
    void remember_error(error err) noexcept;

    std::shared_ptr<const detail::share_key> key;
    event_loop* dispatch_loop;
    std::string method_name;
    std::string url_string;
    std::vector<query_param> query_params;
    std::string body_text;
    std::optional<error> staged_error;
};

}  // namespace kotatsu::http
