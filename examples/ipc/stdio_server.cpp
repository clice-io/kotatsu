#include <cstddef>
#include <cstdint>
#include <print>
#include <string>

#include "kota/ipc/codec/json.h"

namespace ipc = kota::ipc;

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

struct EditParams {
    std::string text;
};

struct LengthParams {};

struct LengthResult {
    std::size_t length = 0;
};

/// Answers with the length of `text`, the copy its handler took.
ipc::RequestResult<LengthParams, LengthResult> length_of(std::string text) {
    co_return LengthResult{.length = text.size()};
}

}  // namespace

int main() {
    kota::event_loop loop;
    auto transport = ipc::StreamTransport::open_stdio(loop);
    if(!transport) {
        std::println(stderr, "failed to open stdio transport: {}", transport.error().message);
        return 1;
    }

    ipc::JSONPeer peer(loop, std::move(*transport));

    peer.on_request("example/add",
                    [](ipc::JSONPeer::RequestContext&,
                       const AddParams& params) -> ipc::RequestResult<AddParams, AddResult> {
                        co_return AddResult{.sum = params.a + params.b};
                    });

    peer.on_notification("example/log", [](const LogParams& params) {
        std::println(stderr, "[example/log] {}", params.text);
    });

    // The text example/length is about; example/edit replaces it.
    std::string document = "hello";
    peer.on_notification("example/edit",
                         [&document](const EditParams& params) { document = params.text; });

    // Not a coroutine: it runs as the request is dispatched, before any message
    // read behind it, so it copies the text as the request found it; the task
    // of length_of() answers from that copy whatever example/edit does before
    // it starts.
    peer.on_request("example/length",
                    [&document](ipc::JSONPeer::RequestContext&, const LengthParams&)
                        -> ipc::RequestResult<LengthParams, LengthResult> {
                        if(document.empty()) {
                            return kota::outcome_error(ipc::Error("the document is empty"));
                        }
                        return length_of(document);
                    });

    std::println(stderr, "JSON-RPC stdio example is ready.");
    std::println(stderr, "Request methods: {}, {}", "example/add", "example/length");
    std::println(stderr, "Notification methods: {}, {}", "example/log", "example/edit");

    loop.schedule(peer.run());
    return loop.run();
}
