#include <cstdint>
#include <string>
#include <string_view>

#include "ipc/harness/codec_json.h"
#include "ipc/harness/peer_fixture.h"
#include "kota/ipc/codec/json.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

// JSONCodec writes and reads params and results under lsp_config, which
// names fields in lowerCamelCase.

namespace kota::ipc {

namespace {

struct RangeAddParams {
    std::int64_t first_value = 0;
    std::int64_t second_value = 0;
};

struct RangeAddResult {
    std::int64_t computed_sum = 0;
};

struct StatusNoteParams {
    std::string display_name;
    std::int64_t retry_count = 0;
};

}  // namespace

}  // namespace kota::ipc

namespace kota::ipc::protocol {

template <>
struct RequestTraits<RangeAddParams> {
    using Result = RangeAddResult;
    constexpr inline static std::string_view method = "test/rangeAdd";
};

template <>
struct NotificationTraits<StatusNoteParams> {
    constexpr inline static std::string_view method = "test/statusNote";
};

}  // namespace kota::ipc::protocol

namespace kota::ipc {

namespace {

using Fixture = test::PeerFixture<test::JSONAdapter>;

ZEST_SUITE(ipc_peer_rename, Fixture) {

ZEST_CASE(request_params_and_result_are_lower_camel) {
    peer.on_request([](Context&, const RangeAddParams& params) -> RequestResult<RangeAddParams> {
        co_return RangeAddResult{.computed_sum = params.first_value + params.second_value};
    });
    remote.send(
        R"({"jsonrpc":"2.0","id":1,"method":"test/rangeAdd","params":{"firstValue":10,"secondValue":20}})");
    remote.end_input();

    auto [ran] = run(peer.run());
    ZEXPECT(ran.has_value());
    auto sent = remote.drain();
    ZASSERT(sent.size() == 1U);
    ZEXPECT(sent[0] == R"({"jsonrpc":"2.0","id":1,"result":{"computedSum":30}})");
}

ZEST_CASE(notification_params_are_lower_camel) {
    std::string name;
    std::int64_t count = 0;
    peer.on_notification([&](const StatusNoteParams& params) {
        name = params.display_name;
        count = params.retry_count;
    });
    remote.send(
        R"({"jsonrpc":"2.0","method":"test/statusNote","params":{"displayName":"alice","retryCount":3}})");
    remote.end_input();

    auto [ran] = run(peer.run());
    ZEXPECT(ran.has_value());
    ZEXPECT(name == "alice");
    ZEXPECT(count == 3);
}

ZEST_CASE(sent_params_and_read_result_are_lower_camel) {
    auto ask = [&]() -> task<RangeAddResult, Error> {
        co_return co_await peer.send_request(RangeAddParams{.first_value = 40, .second_value = 50})
            .or_fail();
    };
    auto remote_side = [&]() -> task<std::optional<std::string>> {
        auto request = co_await remote.receive();
        remote.send(R"({"jsonrpc":"2.0","id":1,"result":{"computedSum":99}})");
        remote.end_input();
        co_return request;
    };

    auto [ran, asked, received] = run(peer.run(), ask(), remote_side());
    ZEXPECT(ran.has_value());
    ZASSERT(asked.has_value());
    ZEXPECT(asked->computed_sum == 99);
    ZASSERT(received.has_value());
    ZASSERT(received->has_value());
    ZEXPECT(
        **received ==
        R"({"jsonrpc":"2.0","id":1,"method":"test/rangeAdd","params":{"firstValue":40,"secondValue":50}})");
}

};  // ZEST_SUITE(ipc_peer_rename)

}  // namespace

}  // namespace kota::ipc
