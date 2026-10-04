#include <array>
#include <cstddef>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <thread>

#include "http/harness/server.h"
#include "kota/http/http.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::http {

namespace {

/// What one thread saw: how many of its requests went through, and the
/// cookies its last request carried.
struct Seen {
    std::size_t done = 0;
    std::optional<std::string> cookie;
};

/// Sends `rounds` pairs of requests with `client` on a loop of its own: one
/// that sets cookie t`index`, one that shows what the jar sends.
void exchange(http::client client, int index, int rounds, Seen& seen) {
    event_loop loop;
    test::HttpServer server(loop, [index](const test::Received& request) {
        if(request.target == "/seed") {
            return test::Reply{.headers = {{"Set-Cookie", std::format("t{}=v; Path=/", index)}}};
        }
        return test::Reply{};
    });
    if(!server.listening()) {
        return;
    }
    auto api = client.on(loop);
    auto requests = [&]() -> task<> {
        for(int i = 0; i < rounds; ++i) {
            for(auto path: {"/seed", "/show"}) {
                if(co_await api.get(server.url(path)).send()) {
                    seen.done += 1;
                }
            }
        }
        // The server's listener would keep the loop running.
        loop.stop();
    };
    auto running = requests();
    loop.schedule(running);
    loop.run();
    if(!server.requests().empty()) {
        seen.cookie = server.requests().back().header("cookie");
    }
}

ZEST_SUITE(http_detail_client_threads) {

// curl cannot share a cookie jar between threads: each loop keeps its own.
ZEST_CASE(copies_on_two_threads_keep_a_jar_each) {
    constexpr int rounds = 20;
    auto shared = test::loopback_client();
    std::array<Seen, 2> seen;

    std::thread first(exchange, shared, 0, rounds, std::ref(seen[0]));
    std::thread second(exchange, shared, 1, rounds, std::ref(seen[1]));
    first.join();
    second.join();

    for(std::size_t i = 0; i < seen.size(); ++i) {
        ZEST_CONTEXT("thread {}", i);
        ZEXPECT(seen[i].done == 2U * rounds);
        ZEXPECT(seen[i].cookie == std::format("t{}=v", i));
    }
}

};  // ZEST_SUITE(http_detail_client_threads)

}  // namespace

}  // namespace kota::http
