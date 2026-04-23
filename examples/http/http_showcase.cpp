#include <chrono>
#include <cstdlib>
#include <print>
#include <string>
#include <utility>
#include <vector>

#include "kota/http/detail/manager.h"
#include "kota/http/http.h"
#include "kota/async/async.h"

using namespace std::chrono_literals;
using namespace kota;

namespace {

void print_error(std::string_view label, const http::error& err) {
    std::println("{}: {}", label, http::message(err));
}

http::client build_demo_client() {
    return http::client()
        .default_header("accept", "application/json")
        .default_header("x-demo-client", "eventide")
        .user_agent("eventide-http-showcase/1.0")
        .timeout(10s)
        .redirect(http::redirect_policy::limited(5))
        .referer(true)
        .https_only(false)
        .danger_accept_invalid_certs(false)
        .danger_accept_invalid_hostnames(false)
        .min_tls_version(http::tls_version::tls1_2)
        .max_tls_version(http::tls_version::tls1_3)
        .ca_file("/etc/ssl/certs/ca-certificates.crt");
}

void showcase_request_builders(http::bound_client api) {
    api.get("https://example.com")
        .query("lang", "en")
        .header("accept-language", "en-US")
        .cookies("session=manual; preview=true")
        .timeout(2s);

    api.post("https://api.example.com/items")
        .bearer_auth("demo-token")
        .json_text(R"({"name":"eventide","kind":"demo"})");

    api.post("https://api.example.com/form")
        .basic_auth("demo", "secret")
        .form({
            {"name", "eventide"},
            {"kind", "form"    }
    });

    api.put("https://api.example.com/blob")
        .header("content-type", "text/plain")
        .body("plain request body");

    api.patch("https://api.example.com/items/42")
        .header("content-type", "application/merge-patch+json")
        .body(R"({"enabled":true})");

    api.del("https://api.example.com/items/42").no_proxy();
    api.head("https://example.com");
}

task<> run_showcase(event_loop& loop) {
    auto client = build_demo_client();
    auto api = client.on(loop);
    showcase_request_builders(api);

    client.record_cookie(true);

    auto home = co_await api.get("https://example.com")
                    .header("accept", "text/html")
                    .query("from", "showcase")
                    .send()
                    .catch_cancel();

    if(home.is_cancelled()) {
        std::println("home request cancelled");
        co_return;
    }
    if(home.has_error()) {
        print_error("home request failed", home.error());
        co_return;
    }

    std::println("GET {} -> {}", home->url, home->status);
    std::println("body bytes: {}", home->bytes().size());

    auto head =
        co_await api.head("https://example.com").header("accept", "*/*").send().catch_cancel();
    if(head.is_cancelled()) {
        std::println("head request cancelled");
        co_return;
    }
    if(head.has_error()) {
        print_error("head request failed", head.error());
        co_return;
    }

    std::println("HEAD {} -> {}", head->url, head->status);
    client.record_cookie(false);
    std::println("automatic cookie recording disabled for subsequent requests");
}

}  // namespace

int main() {
    event_loop loop;

    auto root = run_showcase(loop);
    loop.schedule(root);
    loop.run();

    http::manager::unregister_loop(loop);
    return 0;
}
