#include <print>
#include <string>

#include "kota/http/detail/manager.h"
#include "kota/http/http.h"
#include "kota/async/async.h"

using namespace kota;

namespace {

void print_error(std::string_view label, const http::error& err) {
    std::println("{}: {}", label, http::message(err));
}

task<> run_demo(event_loop& loop) {
    http::client client;

    std::println("1) let httpbin set two cookies while automatic recording is enabled");
    auto seeded = co_await client.on(loop)
                      .get("https://httpbin.io/cookies/set?session=jar-demo&theme=light")
                      .send()
                      .catch_cancel();
    if(seeded.is_cancelled()) {
        std::println("seed request cancelled");
        co_return;
    }
    if(seeded.has_error()) {
        print_error("seed request failed", seeded.error());
        co_return;
    }

    std::println("seed response body: {}", seeded->text());

    std::println("\n2) ask httpbin which cookies it sees from the recorded jar");
    auto jar_echo =
        co_await client.on(loop).get("https://httpbin.io/cookies").send().catch_cancel();
    if(jar_echo.is_cancelled()) {
        std::println("jar echo request cancelled");
        co_return;
    }
    if(jar_echo.has_error()) {
        print_error("jar echo request failed", jar_echo.error());
        co_return;
    }

    std::println("jar-backed /cookies response: {}", jar_echo->text());

    std::println("\n3) turn off automatic cookie recording on the client");
    client.record_cookie(false);

    std::println("\n4) send manual cookies on one request with cookies(...)");
    auto manual = co_await client.on(loop)
                      .get("https://httpbin.io/cookies")
                      .cookies("session=manual-demo; theme=manual-only")
                      .send()
                      .catch_cancel();
    if(manual.is_cancelled()) {
        std::println("manual request cancelled");
        co_return;
    }
    if(manual.has_error()) {
        print_error("manual request failed", manual.error());
        co_return;
    }

    std::println("manual-only /cookies response: {}", manual->text());
}

}  // namespace

int main() {
    event_loop loop;
    auto root = run_demo(loop);
    loop.schedule(root);
    loop.run();
    http::manager::unregister_loop(loop);
    return 0;
}
