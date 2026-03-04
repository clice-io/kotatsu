#include <cstdint>
#include <print>
#include <string>
#include <string_view>

#include "eventide/jsonrpc/peer.h"

namespace et = eventide;
namespace jsonrpc = et::jsonrpc;

namespace {

struct AddParams {
    std::int64_t a = 0;
    std::int64_t b = 0;
};

struct AddResult {
    std::int64_t sum = 0;
};

struct LogParams {
    std::string text;
};

}  // namespace

int main() {
    et::event_loop loop;
    auto transport = jsonrpc::StreamTransport::open_stdio(loop);
    if(!transport) {
        std::println(stderr, "failed to open stdio transport: {}", transport.error());
        return 1;
    }

    jsonrpc::Peer peer(loop, std::move(*transport));

    peer.on_request("example/add",
                    [](jsonrpc::RequestContext&,
                       const AddParams& params) -> jsonrpc::RequestResult<AddParams, AddResult> {
                        co_return AddResult{.sum = params.a + params.b};
                    });

    peer.on_notification("example/log", [](const LogParams& params) {
        std::println(stderr, "[example/log] {}", params.text);
    });

    std::println(stderr, "JSON-RPC stdio example is ready.");
    std::println(stderr, "Request method: {}", "example/add");
    std::println(stderr, "Notification method: {}", "example/log");

    loop.schedule(peer.run());
    return loop.run();
}
