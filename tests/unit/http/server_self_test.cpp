#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <format>
#include <functional>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../src/async/io/awaiter.h"
#include "../async/loop_fixture.h"
#include "kota/http/detail/client.h"
#include "kota/http/detail/manager.h"
#include "kota/http/http.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

using namespace std::chrono_literals;

namespace {

std::string lower_ascii(std::string_view text) {
    std::string out(text);
    for(auto& ch: out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::string trim_ascii(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while(begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while(end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::optional<std::size_t> parse_size(std::string_view text) {
    std::size_t value = 0;
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    if(auto [ptr, ec] = std::from_chars(begin, end, value); ec == std::errc() && ptr == end) {
        return value;
    }
    return std::nullopt;
}

struct server_request {
    std::string method;
    std::string target;
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    std::string_view header(std::string_view name) const noexcept {
        if(auto it = headers.find(lower_ascii(name)); it != headers.end()) {
            return it->second;
        }
        return {};
    }
};

struct server_response {
    int status = 200;
    std::vector<std::pair<std::string, std::string>> headers = {};
    std::string body;
    std::chrono::milliseconds delay = 0ms;
};

std::string reason_phrase(int status) {
    switch(status) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        default: return "HTTP";
    }
}

task<std::optional<server_request>, error> read_request(tcp& client) {
    std::string buffer;
    std::size_t header_end = std::string::npos;

    while((header_end = buffer.find("\r\n\r\n")) == std::string::npos) {
        auto chunk = co_await client.read_chunk();
        if(!chunk) {
            if(chunk.error() == error::end_of_file && buffer.empty()) {
                co_return std::nullopt;
            }
            co_await fail(chunk.error());
        }

        buffer.append(chunk->data(), chunk->size());
        client.consume(chunk->size());
    }

    server_request request;
    const auto headers_block = buffer.substr(0, header_end);
    const auto first_line_end = headers_block.find("\r\n");
    const auto request_line = headers_block.substr(0, first_line_end);

    const auto first_space = request_line.find(' ');
    const auto second_space = request_line.find(' ', first_space + 1);
    if(first_space == std::string::npos || second_space == std::string::npos) {
        co_return std::nullopt;
    }

    request.method = request_line.substr(0, first_space);
    request.target = request_line.substr(first_space + 1, second_space - first_space - 1);

    std::size_t content_length = 0;
    std::size_t line_start =
        first_line_end == std::string::npos ? headers_block.size() : first_line_end + 2;
    while(line_start < headers_block.size()) {
        const auto line_end = headers_block.find("\r\n", line_start);
        const auto line = headers_block.substr(
            line_start,
            line_end == std::string::npos ? std::string::npos : line_end - line_start);
        const auto colon = line.find(':');
        if(colon != std::string::npos) {
            auto name = lower_ascii(trim_ascii(line.substr(0, colon)));
            auto value = trim_ascii(line.substr(colon + 1));
            if(name == "content-length") {
                content_length = parse_size(value).value_or(0);
            }
            request.headers.insert_or_assign(std::move(name), std::move(value));
        }

        if(line_end == std::string::npos) {
            break;
        }
        line_start = line_end + 2;
    }

    const auto body_start = header_end + 4;
    while(buffer.size() < body_start + content_length) {
        auto chunk = co_await client.read_chunk();
        if(!chunk) {
            co_await fail(chunk.error());
        }
        buffer.append(chunk->data(), chunk->size());
        client.consume(chunk->size());
    }

    request.body = buffer.substr(body_start, content_length);
    co_return request;
}

task<> write_response(tcp& client, server_response response, event_loop& loop) {
    if(response.delay.count() > 0) {
        co_await sleep(response.delay, loop);
    }

    std::string payload =
        std::format("HTTP/1.1 {} {}\r\n", response.status, reason_phrase(response.status));

    bool has_content_length = false;
    bool has_connection = false;
    for(const auto& [name, value]: response.headers) {
        has_content_length = has_content_length || lower_ascii(name) == "content-length";
        has_connection = has_connection || lower_ascii(name) == "connection";
        payload += std::format("{}: {}\r\n", name, value);
    }

    if(!has_content_length) {
        payload += std::format("Content-Length: {}\r\n", response.body.size());
    }
    if(!has_connection) {
        payload += "Connection: close\r\n";
    }

    payload += "\r\n";
    payload += response.body;

    [[maybe_unused]] auto err =
        co_await client.write(std::span<const char>(payload.data(), payload.size()));
}

class test_http_server {
public:
    using handler_type = std::function<server_response(const server_request&)>;

    test_http_server(event_loop& ev_loop, handler_type cb) :
        loop(&ev_loop), handler(std::move(cb)) {
        auto listener = tcp::listen("127.0.0.1", 0, {}, ev_loop);
        if(!listener) {
            return;
        }

        acceptor = std::move(*listener);
        auto local_port = tcp::local_port(acceptor);
        if(!local_port) {
            acceptor.stop();
            return;
        }

        port = *local_port;
        uv::unref(acceptor->stream);
        server_task = serve();
        loop->schedule(server_task);
    }

    ~test_http_server() {
        stop();
    }

    test_http_server(const test_http_server&) = delete;
    test_http_server& operator=(const test_http_server&) = delete;

    std::string url(std::string_view path) const {
        return std::format("http://127.0.0.1:{}{}", port, path);
    }

    bool valid() const noexcept {
        return port > 0;
    }

    void stop() {
        if(stopped.exchange(true) || !valid()) {
            return;
        }

        auto moved_acceptor = std::move(acceptor);
        [[maybe_unused]] auto stop_err = moved_acceptor.stop();
        moved_acceptor = {};
        loop->run();
        auto result = server_task.result();
        if(result.has_error()) {
            auto err = result.error();
            EXPECT_TRUE(err == error::operation_aborted);
        }
        port = 0;
    }

private:
    task<> handle_client(tcp client) {
        auto request = co_await read_request(client).catch_cancel();
        if(request.is_cancelled() || request.has_error() || !*request) {
            co_return;
        }

        auto response = handler ? handler(**request) : server_response{};
        co_await write_response(client, std::move(response), *loop);
    }

    task<void, error> serve() {
        while(true) {
            auto client = co_await acceptor.accept();
            if(!client) {
                if(client.error() == error::operation_aborted) {
                    co_return;
                }
                co_await fail(client.error());
            }

            loop->schedule(handle_client(std::move(*client)));
        }
    }

    event_loop* loop = nullptr;
    tcp::acceptor acceptor{};
    handler_type handler{};
    task<void, error> server_task{};
    std::atomic<bool> stopped = false;
    int port = 0;
};

struct http_loop_fixture : loop_fixture {
    ~http_loop_fixture() {
        http::manager::unregister_loop(loop);
    }
};

template <typename Task>
auto run_task(http_loop_fixture& fixture, Task& task) {
    fixture.loop.schedule(task);
    fixture.loop.run();
    return task.result();
}

task<std::string, http::error> when_all_fetch(http::bound_client api,
                                              std::string left_url,
                                              std::string right_url) {
    auto both = co_await when_all(api.get(std::move(left_url)).send(),
                                  api.get(std::move(right_url)).send());
    if(both.has_error()) {
        co_await fail(std::move(both).error());
    }

    auto [left, right] = *both;
    co_return std::format("{}|{}", left.text_copy(), right.text_copy());
}

task<std::string, http::error> interleaved_fetch(http::bound_client api,
                                                 std::string first_url,
                                                 std::string second_url,
                                                 event& gate) {
    auto first = co_await api.get(std::move(first_url)).send().or_fail();
    co_await gate.wait();
    auto second = co_await api.get(std::move(second_url)).send().or_fail();
    co_return std::format("{}|{}", first.text_copy(), second.text_copy());
}

task<void, http::error>
    destroy_sibling_after_first(http::bound_client api,
                                std::string first_url,
                                std::optional<task<http::response, http::error>>& sibling) {
    auto first = co_await api.get(std::move(first_url)).send().or_fail();
    EXPECT_EQ(first.text(), "/first");
    sibling.reset();
}

task<std::string, http::error> recreate_manager_between_requests(http::client& client,
                                                                 event_loop& loop,
                                                                 std::string first_url,
                                                                 std::string second_url) {
    auto first = co_await client.get(std::move(first_url)).send().or_fail();
    http::manager::unregister_loop(loop);
    auto second = co_await client.get(std::move(second_url)).send().or_fail();
    co_return std::format("{}|{}", first.text_copy(), second.text_copy());
}

}  // namespace

TEST_SUITE(http_client, http_loop_fixture) {

TEST_CASE(builder_overrides_preserve_manual_cookie) {
    http::client client(
        loop,
        {.proxy_config = http::proxy{.url = "http://proxy.internal:9000"}, .record_cookie = true});

    auto built =
        client.get("http://example.test").cookies("manual=1").no_proxy().timeout(25ms).build();

    EXPECT_TRUE(built.record_cookie);
    EXPECT_TRUE(built.disable_proxy);
    EXPECT_FALSE(built.proxy_config.has_value());
    EXPECT_TRUE(built.timeout.has_value());
    EXPECT_EQ(*built.timeout, 25ms);
    EXPECT_EQ(built.cookie, "manual=1");
}

TEST_CASE(client_can_be_built_unbound_and_bound_later) {
    auto client = http::client::builder().user_agent("late-bind").build();

    ASSERT_TRUE(client.has_value());
    EXPECT_FALSE(client->is_bound());

    client->record_cookie(false);
    client->bind(loop);
    EXPECT_TRUE(client->is_bound());

    auto built = client->get("https://example.test/api").build();
    EXPECT_EQ(built.user_agent, "late-bind");
    EXPECT_FALSE(built.record_cookie);
}

TEST_CASE(unbound_client_can_dispatch_via_on) {
    test_http_server server(loop, [](const server_request& request) -> server_response {
        return {.body = request.target};
    });
    ASSERT_TRUE(server.valid());

    http::client client;
    EXPECT_FALSE(client.is_bound());
    EXPECT_FALSE(client.loop().has_value());

    auto req = client.on(loop).get(server.url("/via-on")).send();
    auto result = run_task(*this, req);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->text(), "/via-on");
}

TEST_CASE(request_builder_keeps_client_state_alive_after_client_destruction) {
    test_http_server server(loop, [](const server_request& request) -> server_response {
        if(request.target == "/seed") {
            return {.headers = {{"Set-Cookie", "builder_cookie=1; Path=/"}}, .body = "seed"};
        }
        return {.body = std::string(request.header("cookie"))};
    });
    ASSERT_TRUE(server.valid());

    auto builder = [&]() {
        http::client client(loop);
        auto seed = client.get(server.url("/seed")).send();
        auto seed_result = run_task(*this, seed);
        EXPECT_TRUE(seed_result.has_value());
        return client.get(server.url("/echo"));
    }();

    auto req = std::move(builder).send();
    auto result = run_task(*this, req);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->text(), "builder_cookie=1");
}

TEST_CASE(bound_client_keeps_client_state_alive_after_client_destruction) {
    test_http_server server(loop, [](const server_request& request) -> server_response {
        if(request.target == "/seed") {
            return {.headers = {{"Set-Cookie", "bound_cookie=1; Path=/"}}, .body = "seed"};
        }
        return {.body = std::string(request.header("cookie"))};
    });
    ASSERT_TRUE(server.valid());

    auto api = [&]() {
        http::client client(loop);
        auto seed = client.get(server.url("/seed")).send();
        auto seed_result = run_task(*this, seed);
        EXPECT_TRUE(seed_result.has_value());
        return client.on(loop);
    }();

    auto req = api.get(server.url("/echo")).send();
    auto result = run_task(*this, req);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->text(), "bound_cookie=1");
}

#if KOTA_HTTP_HAS_CODEC_JSON
TEST_CASE(request_builder_json_sets_body_and_content_type) {
    http::client client;

    auto builder = client.post("https://example.test/json");
    builder.json(std::vector<int>{1, 2, 3});

    auto built = std::move(builder).build();
    EXPECT_EQ(built.body, "[1,2,3]");
    auto content_type =
        std::find_if(built.headers.begin(), built.headers.end(), [](const http::header& item) {
            return item.name == "content-type";
        });
    ASSERT_NE(content_type, built.headers.end());
    EXPECT_EQ(content_type->value, "application/json");
}
#endif

TEST_CASE(response_body_exposes_bytes_and_text_helpers) {
    http::response response;
    response.body = {std::byte{'o'}, std::byte{'k'}};

    EXPECT_EQ(response.bytes().size(), std::size_t(2));
    EXPECT_EQ(response.text(), "ok");
    EXPECT_EQ(response.text_copy(), "ok");
}

TEST_CASE(http_error_message_member_matches_free_function) {
    auto err = http::error::invalid_request("bad request");
    EXPECT_EQ(err.message(), "bad request");
    EXPECT_EQ(err.message(), http::message(err));
}

TEST_CASE(unbound_client_send_fails_cleanly) {
    http::client client;
    EXPECT_FALSE(client.is_bound());

    auto req = client.get("https://example.com").send();
    auto result = run_task(*this, req);
    EXPECT_TRUE(result.has_error());
    EXPECT_EQ(result.error().kind, http::error_kind::invalid_request);
}

TEST_CASE(client_builder_applies_redirect_and_tls_defaults) {
    auto client = http::client::builder(loop)
                      .user_agent("eventide-test")
                      .redirect(http::redirect_policy::limited(3))
                      .referer(false)
                      .https_only()
                      .danger_accept_invalid_certs()
                      .danger_accept_invalid_hostnames()
                      .min_tls_version(http::tls_version::tls1_2)
                      .max_tls_version(http::tls_version::tls1_3)
                      .timeout(42ms)
                      .build();

    ASSERT_TRUE(client.has_value());
    client->record_cookie(false);

    auto built = client->get("https://example.test/api").build();
    EXPECT_EQ(built.user_agent, "eventide-test");
    EXPECT_FALSE(built.record_cookie);
    EXPECT_EQ(built.redirect.max_redirects, std::size_t(3));
    EXPECT_FALSE(built.redirect.referer);
    EXPECT_TRUE(built.tls.https_only);
    EXPECT_TRUE(built.tls.danger_accept_invalid_certs);
    EXPECT_TRUE(built.tls.danger_accept_invalid_hostnames);
    ASSERT_TRUE(built.tls.min_version.has_value());
    ASSERT_TRUE(built.tls.max_version.has_value());
    EXPECT_EQ(*built.tls.min_version, http::tls_version::tls1_2);
    EXPECT_EQ(*built.tls.max_version, http::tls_version::tls1_3);
    ASSERT_TRUE(built.timeout.has_value());
    EXPECT_EQ(*built.timeout, 42ms);
}

TEST_CASE(invalid_tls_range_is_rejected_during_request_prepare) {
    auto client = http::client::builder(loop)
                      .min_tls_version(http::tls_version::tls1_3)
                      .max_tls_version(http::tls_version::tls1_2)
                      .build();

    ASSERT_TRUE(client.has_value());

    auto req = client->get("https://example.com").send();
    auto result = run_task(*this, req);
    EXPECT_TRUE(result.has_error());
    EXPECT_EQ(result.error().kind, http::error_kind::invalid_request);
}

TEST_CASE(cookie_store_persists_and_response_headers_are_captured) {
    test_http_server server(loop, [](const server_request& request) -> server_response {
        if(request.target == "/seed") {
            return {
                .headers = {{"Set-Cookie", "session=alpha; Path=/"}, {"X-Test", "seed"}},
                .body = "seed",
            };
        }

        return {.body = std::string(request.header("cookie"))};
    });
    ASSERT_TRUE(server.valid());

    http::client client(loop);

    auto seed = client.get(server.url("/seed")).send();
    auto seed_result = run_task(*this, seed);
    ASSERT_TRUE(seed_result.has_value());
    auto header = seed_result->header_value("x-test");
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(*header, "seed");

    auto follow = client.get(server.url("/echo-cookie")).send();
    auto follow_result = run_task(*this, follow);
    ASSERT_TRUE(follow_result.has_value());
    EXPECT_EQ(follow_result->text(), "session=alpha");
}

TEST_CASE(cookie_store_isolated_between_clients_on_same_loop) {
    test_http_server server(loop, [](const server_request& request) -> server_response {
        if(request.target == "/seed") {
            return {.headers = {{"Set-Cookie", "session=left; Path=/"}}, .body = "seed"};
        }

        return {.body = std::string(request.header("cookie"))};
    });
    ASSERT_TRUE(server.valid());

    http::client left(loop);
    http::client right(loop);

    auto seed = left.get(server.url("/seed")).send();
    auto seed_result = run_task(*this, seed);
    ASSERT_TRUE(seed_result.has_value());

    auto right_check = right.get(server.url("/echo-cookie")).send();
    auto right_result = run_task(*this, right_check);
    ASSERT_TRUE(right_result.has_value());
    EXPECT_TRUE(right_result->bytes().empty());

    auto left_check = left.get(server.url("/echo-cookie")).send();
    auto left_result = run_task(*this, left_check);
    ASSERT_TRUE(left_result.has_value());
    EXPECT_EQ(left_result->text(), "session=left");
}

TEST_CASE(request_cookie_string_is_forwarded_verbatim) {
    test_http_server server(loop, [](const server_request& request) -> server_response {
        return {.body = std::string(request.header("cookie"))};
    });
    ASSERT_TRUE(server.valid());

    http::client client(loop);
    client.record_cookie(false);

    auto req = client.get(server.url("/echo-cookie")).cookies("session=manual").send();
    auto result = run_task(*this, req);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->text(), "session=manual");
}

TEST_CASE(record_cookie_false_disables_store_but_keeps_manual_cookies) {
    test_http_server server(loop, [](const server_request& request) -> server_response {
        if(request.target == "/seed") {
            return {.headers = {{"Set-Cookie", "session=jar; Path=/"}}, .body = "seed"};
        }

        return {.body = std::string(request.header("cookie"))};
    });
    ASSERT_TRUE(server.valid());

    http::client client(loop);

    auto seed = client.get(server.url("/seed")).send();
    auto seed_result = run_task(*this, seed);
    ASSERT_TRUE(seed_result.has_value());

    client.record_cookie(false);

    auto req = client.get(server.url("/echo-cookie")).cookies("manual=1").send();
    auto result = run_task(*this, req);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->text(), "manual=1");
}

TEST_CASE(record_cookie_false_disables_automatic_cookie_handling_for_future_requests) {
    test_http_server server(loop, [](const server_request& request) -> server_response {
        if(request.target == "/seed") {
            return {.headers = {{"Set-Cookie", "session=jar; Path=/"}}, .body = "seed"};
        }
        if(request.target == "/seed-disabled") {
            return {.headers = {{"Set-Cookie", "session=disabled; Path=/"}}, .body = "seed"};
        }

        return {.body = std::string(request.header("cookie"))};
    });
    ASSERT_TRUE(server.valid());

    http::client client(loop);

    auto seed = client.get(server.url("/seed")).send();
    auto seed_result = run_task(*this, seed);
    ASSERT_TRUE(seed_result.has_value());

    client.record_cookie(false);

    auto disabled_seed = client.get(server.url("/seed-disabled")).send();
    auto disabled_seed_result = run_task(*this, disabled_seed);
    ASSERT_TRUE(disabled_seed_result.has_value());

    auto req = client.get(server.url("/echo-cookie")).send();
    auto result = run_task(*this, req);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->bytes().empty());
}

TEST_CASE(query_parameters_are_encoded_and_headers_are_upserted) {
    test_http_server server(loop, [](const server_request& request) -> server_response {
        return {.body = std::format("{}|{}", request.target, request.header("x-test"))};
    });
    ASSERT_TRUE(server.valid());

    http::client client(loop);

    auto req = client.get(server.url("/inspect"))
                   .query("q", "a b")
                   .query("path", "x/y")
                   .header("x-test", "one")
                   .header("X-Test", "two")
                   .send();
    auto result = run_task(*this, req);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->text(), "/inspect?q=a%20b&path=x%2Fy|two");
}

TEST_CASE(form_request_sets_content_type_and_encodes_body) {
    test_http_server server(loop, [](const server_request& request) -> server_response {
        return {.body = std::format("{}|{}", request.header("content-type"), request.body)};
    });
    ASSERT_TRUE(server.valid());

    http::client client(loop);

    auto req = client.post(server.url("/form"))
                   .form({
                       {"name", "alice"},
                       {"note", "a b+c"}
    })
                   .send();
    auto result = run_task(*this, req);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->text(), "application/x-www-form-urlencoded|name=alice&note=a%20b%2Bc");
}

TEST_CASE(get_request_with_body_is_rejected) {
    http::client client(loop);

    auto req = client.get("https://example.com").body("unexpected").send();
    auto result = run_task(*this, req);
    EXPECT_TRUE(result.has_error());
    EXPECT_EQ(result.error().kind, http::error_kind::invalid_request);
    EXPECT_EQ(result.error().message(), "request body is not supported for GET or HEAD");
}

TEST_CASE(head_request_with_body_is_rejected) {
    http::client client(loop);

    auto req = client.head("https://example.com").body("unexpected").send();
    auto result = run_task(*this, req);
    EXPECT_TRUE(result.has_error());
    EXPECT_EQ(result.error().kind, http::error_kind::invalid_request);
    EXPECT_EQ(result.error().message(), "request body is not supported for GET or HEAD");
}

TEST_CASE(head_request_captures_headers_and_skips_body) {
    test_http_server server(loop, [](const server_request& request) -> server_response {
        EXPECT_EQ(request.method, "HEAD");
        return {.headers = {{"X-Mode", "head"}}, .body = "ignored"};
    });
    ASSERT_TRUE(server.valid());

    http::client client(loop);

    auto req = client.head(server.url("/only-headers")).send();
    auto result = run_task(*this, req);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, 200);
    EXPECT_TRUE(result->bytes().empty());
    auto header = result->header_value("x-mode");
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(*header, "head");
}

TEST_CASE(redirect_policy_follows_by_default_when_enabled) {
    test_http_server server(loop, [&](const server_request& request) -> server_response {
        if(request.target == "/jump") {
            return {.status = 302,
                    .headers = {{"Location", server.url("/final")}},
                    .body = "redirect"};
        }

        return {.body = "final"};
    });
    ASSERT_TRUE(server.valid());

    auto client = http::client::builder(loop).redirect(http::redirect_policy::limited(4)).build();
    ASSERT_TRUE(client.has_value());

    auto req = client->get(server.url("/jump")).send();
    auto result = run_task(*this, req);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, 200);
    EXPECT_EQ(result->text(), "final");
    EXPECT_EQ(result->url, server.url("/final"));
}

TEST_CASE(redirect_policy_none_preserves_redirect_response) {
    test_http_server server(loop, [&](const server_request& request) -> server_response {
        if(request.target == "/jump") {
            return {.status = 302,
                    .headers = {{"Location", server.url("/final")}},
                    .body = "redirect"};
        }

        return {.body = "final"};
    });
    ASSERT_TRUE(server.valid());

    auto client = http::client::builder(loop).redirect(http::redirect_policy::none()).build();
    ASSERT_TRUE(client.has_value());

    auto req = client->get(server.url("/jump")).send();
    auto result = run_task(*this, req);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, 302);
    EXPECT_EQ(result->text(), "redirect");
    auto location = result->header_value("location");
    ASSERT_TRUE(location.has_value());
    EXPECT_EQ(*location, server.url("/final"));
}

TEST_CASE(custom_http_method_is_forwarded_verbatim) {
    test_http_server server(loop, [](const server_request& request) -> server_response {
        return {.body = std::format("{} {}", request.method, request.target)};
    });
    ASSERT_TRUE(server.valid());

    http::client client(loop);
    auto req = client.request("OPTIONS", server.url("/caps")).send();
    auto result = run_task(*this, req);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->text(), "OPTIONS /caps");
}

TEST_CASE(custom_curl_string_option_can_override_user_agent) {
    test_http_server server(loop, [](const server_request& request) -> server_response {
        return {.body = std::string(request.header("user-agent"))};
    });
    ASSERT_TRUE(server.valid());

    http::client client(loop);
    auto req =
        client.get(server.url("/ua")).curl_option(CURLOPT_USERAGENT, "curl-opt-agent/1.0").send();
    auto result = run_task(*this, req);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->text(), "curl-opt-agent/1.0");
}

TEST_CASE(custom_curl_long_option_can_override_redirect_behavior) {
    test_http_server server(loop, [&](const server_request& request) -> server_response {
        if(request.target == "/jump") {
            return {.status = 302,
                    .headers = {{"Location", server.url("/final")}},
                    .body = "redirect"};
        }

        return {.body = "final"};
    });
    ASSERT_TRUE(server.valid());

    http::client client(loop);
    auto req = client.get(server.url("/jump")).curl_option(CURLOPT_FOLLOWLOCATION, 0L).send();
    auto result = run_task(*this, req);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, 302);
    EXPECT_EQ(result->text(), "redirect");
}

TEST_CASE(https_only_rejects_plain_http_requests) {
    test_http_server server(loop, [](const server_request&) -> server_response {
        return {.body = "plain"};
    });
    ASSERT_TRUE(server.valid());

    auto client = http::client::builder(loop).https_only().build();
    ASSERT_TRUE(client.has_value());

    auto req = client->get(server.url("/plain")).send();
    auto result = run_task(*this, req);
    EXPECT_TRUE(result.has_error());
    EXPECT_EQ(result.error().kind, http::error_kind::curl);
    EXPECT_EQ(http::manager::for_loop(loop).pending_requests(), std::size_t(0));
}

TEST_CASE(many_concurrent_requests_complete) {
    constexpr int count = 64;
    std::atomic<int> seen = 0;

    test_http_server server(loop, [&](const server_request& request) -> server_response {
        seen.fetch_add(1);
        return {.headers = {{"X-Target", request.target}}, .body = request.target};
    });
    ASSERT_TRUE(server.valid());

    http::client client(loop);
    std::vector<task<http::response, http::error>> tasks;
    tasks.reserve(count);

    for(int i = 0; i < count; ++i) {
        tasks.push_back(client.get(server.url(std::format("/req/{}", i))).send());
    }

    for(auto& task: tasks) {
        loop.schedule(task);
    }
    loop.run();

    EXPECT_EQ(seen.load(), count);
    EXPECT_EQ(http::manager::for_loop(loop).pending_requests(), std::size_t(0));

    for(int i = 0; i < count; ++i) {
        auto result = tasks[i].result();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->text(), std::format("/req/{}", i));
        auto target = result->header_value("x-target");
        ASSERT_TRUE(target.has_value());
        EXPECT_EQ(*target, std::format("/req/{}", i));
    }
}

TEST_CASE(in_flight_request_survives_client_destruction) {
    test_http_server server(loop, [](const server_request& request) -> server_response {
        if(request.target == "/seed") {
            return {.headers = {{"Set-Cookie", "in_flight=1; Path=/"}}, .body = "seed"};
        }
        return {.body = std::string(request.header("cookie")), .delay = 25ms};
    });
    ASSERT_TRUE(server.valid());

    std::optional<http::client> client(std::in_place, loop);
    auto seed = client->get(server.url("/seed")).send();
    auto seed_result = run_task(*this, seed);
    ASSERT_TRUE(seed_result.has_value());

    auto req = client->get(server.url("/slow-cookie")).send();
    client.reset();

    auto result = run_task(*this, req);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->text(), "in_flight=1");
    EXPECT_EQ(http::manager::for_loop(loop).pending_requests(), std::size_t(0));
}

TEST_CASE(multiple_http_requests_can_be_coawaited_with_when_all) {
    test_http_server server(loop, [](const server_request& request) -> server_response {
        return {.body = request.target};
    });
    ASSERT_TRUE(server.valid());

    http::client client(loop);
    auto flow = when_all_fetch(client.on(loop), server.url("/left"), server.url("/right"));
    auto result = run_task(*this, flow);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "/left|/right");
}

TEST_CASE(http_requests_can_interleave_with_uv_events) {
    test_http_server server(loop, [](const server_request& request) -> server_response {
        return {.body = request.target};
    });
    ASSERT_TRUE(server.valid());

    http::client client(loop);
    event gate;
    auto flow =
        interleaved_fetch(client.on(loop), server.url("/first"), server.url("/second"), gate);
    auto release_gate_fn = [&]() -> task<> {
        co_await sleep(1ms, loop);
        gate.set();
    };
    auto release_gate = release_gate_fn();

    loop.schedule(flow);
    loop.schedule(release_gate);
    loop.run();

    auto result = flow.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "/first|/second");
    EXPECT_EQ(http::manager::for_loop(loop).pending_requests(), std::size_t(0));
}

TEST_CASE(cancelled_request_does_not_break_following_requests) {
    test_http_server server(loop, [](const server_request& request) -> server_response {
        if(request.target == "/slow") {
            return {.body = "slow", .delay = 200ms};
        }

        return {.body = "ok"};
    });
    ASSERT_TRUE(server.valid());

    http::client client(loop);

    auto request = client.get(server.url("/slow")).send().catch_cancel();
    auto cancel_after = [](task<http::response, http::error, cancellation>* pending,
                           event_loop& ev) -> task<> {
        co_await sleep(20ms, ev);
        (*pending)->cancel();
    };
    auto canceler = cancel_after(&request, loop);

    loop.schedule(request);
    loop.schedule(canceler);
    loop.run();

    auto cancelled = request.result();
    EXPECT_TRUE(cancelled.is_cancelled());
    EXPECT_EQ(http::manager::for_loop(loop).pending_requests(), std::size_t(0));

    auto next = client.get(server.url("/ok")).send();
    auto next_result = run_task(*this, next);
    ASSERT_TRUE(next_result.has_value());
    EXPECT_EQ(next_result->text(), "ok");
    EXPECT_EQ(http::manager::for_loop(loop).pending_requests(), std::size_t(0));
}

TEST_CASE(destroying_a_sibling_task_after_http_completion_keeps_manager_healthy) {
    test_http_server server(loop, [](const server_request& request) -> server_response {
        server_response response;
        response.body = request.target;
        response.delay = 25ms;
        return response;
    });
    ASSERT_TRUE(server.valid());

    http::client client(loop);
    std::optional<task<http::response, http::error>> sibling(
        std::in_place,
        client.get(server.url("/second")).send());
    auto flow = destroy_sibling_after_first(client.on(loop), server.url("/first"), sibling);

    loop.schedule(flow);
    loop.schedule(*sibling);
    loop.run();

    auto flow_result = flow.result();
    ASSERT_TRUE(flow_result.has_value());
    EXPECT_FALSE(sibling.has_value());
    EXPECT_EQ(http::manager::for_loop(loop).pending_requests(), std::size_t(0));

    auto next = client.get(server.url("/after")).send();
    auto next_result = run_task(*this, next);
    ASSERT_TRUE(next_result.has_value());
    EXPECT_EQ(next_result->text(), "/after");
}

TEST_CASE(http_completion_can_recreate_manager_inline) {
    test_http_server server(loop, [](const server_request& request) -> server_response {
        server_response response;
        response.body = request.target;
        return response;
    });
    ASSERT_TRUE(server.valid());

    http::client client(loop);
    auto flow = recreate_manager_between_requests(client,
                                                  loop,
                                                  server.url("/first"),
                                                  server.url("/second"));
    auto result = run_task(*this, flow);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "/first|/second");
    EXPECT_EQ(http::manager::for_loop(loop).pending_requests(), std::size_t(0));
}

};  // TEST_SUITE(http_client)

}  // namespace kota
