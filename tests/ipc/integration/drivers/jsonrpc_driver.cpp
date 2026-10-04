// A JSON-RPC peer on stdio for ipc's integration tests. Its methods:
//
//   test/echo         request: answers with its params
//   test/sleep        request {ms}: answers null after ms milliseconds, or
//                     RequestCancelled once the client cancels it
//   test/fail         request {code, message}: fails with that error
//   test/notify       request {method, params, count}: sends the client that
//                     notification count times, then answers null
//   test/call         request {method, params, timeoutMs}: sends the client
//                     that request, and answers with how it ended, {result}
//                     or {error}
//   test/closeOutput  request: closes the output, once what is queued is
//                     written, the answers queued before its task runs
//                     included; its own answer is dropped
//
// `--max-payload=<bytes>` lowers the size of the largest frame it reads from
// the default. It exits with 0 when its input ends.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>

#include "ipc/harness/stderr_logger.h"
#include "kota/ipc/codec/json.h"
#include "kota/async/async.h"
#include "kota/codec/macro.h"

namespace kota::test {
namespace {

using ipc::protocol::Error;
using Value = codec::dyn::Value;
using Context = ipc::JSONPeer::RequestContext;

struct SleepParams {
    std::uint32_t ms = 0;
};

struct FailParams {
    std::int32_t code = 0;
    std::string message;
};

struct NotifyParams {
    std::string method;
    Value params;
    std::uint32_t count = 0;
};

struct CallParams {
    std::string method;
    Value params;
    std::optional<std::uint32_t> timeout_ms;
};

// Exactly one of the two, the other left out.
struct CallOutcome {
    KOTATSU_ANNOTATE(skip_if = skip_when::none)
    <std::optional<Value>> result;
    KOTATSU_ANNOTATE(skip_if = skip_when::none)
    <std::optional<Error>> error;
};

struct CloseOutputParams {};

void serve(ipc::JSONPeer& peer) {
    peer.on_request("test/echo",
                    [](Context&, const Value& params) -> task<Value, Error> { co_return params; });

    peer.on_request("test/sleep",
                    [](Context&, const SleepParams& params) -> task<std::nullptr_t, Error> {
                        co_await sleep(std::chrono::milliseconds(params.ms));
                        co_return nullptr;
                    });

    peer.on_request("test/fail",
                    [](Context&, const FailParams& params) -> task<std::nullptr_t, Error> {
                        co_return outcome_error(Error(params.code, params.message));
                    });

    peer.on_request(
        "test/notify",
        [](Context& context, const NotifyParams& params) -> task<std::nullptr_t, Error> {
            for(std::uint32_t i = 0; i < params.count; ++i) {
                co_await or_fail(context->send_notification(params.method, params.params));
            }
            co_return nullptr;
        });

    peer.on_request("test/call",
                    [](Context& context, const CallParams& params) -> task<CallOutcome, Error> {
                        ipc::request_options options;
                        if(params.timeout_ms) {
                            options.timeout = std::chrono::milliseconds(*params.timeout_ms);
                        }
                        auto result = co_await context->send_request<Value>(params.method,
                                                                            params.params,
                                                                            std::move(options));
                        if(result.has_error()) {
                            co_return CallOutcome{.result = std::nullopt,
                                                  .error = std::move(result).error()};
                        }
                        co_return CallOutcome{.result = std::move(*result), .error = std::nullopt};
                    });

    peer.on_request("test/closeOutput",
                    [](Context& context, const CloseOutputParams&) -> task<std::nullptr_t, Error> {
                        context->close_output();
                        co_return nullptr;
                    });
}

}  // namespace
}  // namespace kota::test

int main(int argc, char** argv) {
    std::size_t max_payload = kota::ipc::default_max_payload;
    constexpr std::string_view option = "--max-payload=";
    if(argc > 1 && std::string_view(argv[1]).starts_with(option)) {
        max_payload = std::stoull(argv[1] + option.size());
    }

    kota::event_loop loop;
    auto transport = kota::ipc::StreamTransport::open_stdio(loop, max_payload);
    if(!transport) {
        std::println(stderr, "[error] open_stdio: {}", transport.error().message);
        return 1;
    }

    kota::ipc::JSONPeer peer(loop, std::move(*transport));
    peer.set_logger(kota::test::stderr_logger(), kota::ipc::LogLevel::trace);
    kota::test::serve(peer);
    loop.schedule(peer.run());
    loop.run();
    return 0;
}
