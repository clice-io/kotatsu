#include <chrono>
#include <print>
#include <string>

#include "kota/http/detail/manager.h"
#include "kota/http/http.h"
#include "kota/async/async.h"
#include "kota/codec/json/json.h"

using namespace std::chrono_literals;
using namespace kota;

namespace {

struct response_request {
    std::string model;
    std::string input;
};

task<void, http::error> request_openai(event_loop& loop) {
    http::client client;
    client.timeout(60s);
    auto api = client.on(loop);

    auto result = co_await api.post("http://.../v1/responses")
                      .header("authorization", "Bearer sk-114514")
                      .json(response_request{
                          .model = "gpt-5.4",
                          .input = "Do you know ykiko and her project clice, a cpp lsp?",
                      })
                      .send()
                      .or_fail();

    auto parsed = codec::json::Value::parse(result.text()).value();
    auto reply = parsed.as_ref()
                     .get_object()
                     .value()["output"]
                     .get_array()
                     .value()[0]
                     .get_object()
                     .value()["content"]
                     .get_array()
                     .value()[0]
                     .get_object()
                     .value()["text"]
                     .as_string();

    std::println("status: {}", result.status);
    std::println("{}", reply);
}

}  // namespace

int main() {
    event_loop loop;
    auto root = request_openai(loop);
    loop.schedule(root);
    loop.run();
    http::manager::unregister_loop(loop);
    return 0;
}
