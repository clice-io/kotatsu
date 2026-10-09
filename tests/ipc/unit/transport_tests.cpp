#include <string>
#include <string_view>
#include <vector>

#include "kota/ipc/transport.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::ipc {

namespace {

/// A transport that keeps what it writes, and fails the write of `failing`.
struct Collecting final : Transport {
    std::string_view failing;
    std::vector<std::string> written = {};

    explicit Collecting(std::string_view failing) : failing(failing) {}

    task<std::string, ReadError> read_message() override {
        co_await fail(ReadError{});
    }

    task<void, Error> write_message(std::string_view payload) override {
        if(payload == failing) {
            co_await fail(Error("write failed"));
        }
        written.emplace_back(payload);
    }

    task<void, Error> close_output() override {
        co_return;
    }

    Result<void> close() override {
        return {};
    }
};

ZEST_SUITE(ipc_transport, zest::LoopFixture) {

// A transport that has no write of its own for several writes them one by
// one.
ZEST_CASE(write_messages_writes_each_in_order) {
    Collecting transport("none of these");
    const std::vector<std::string> payloads = {"first", "", "third"};

    auto [written] = run(transport.write_messages(payloads));
    ZEXPECT(written.has_value());
    ZEXPECT(transport.written == payloads);
}

ZEST_CASE(write_messages_stops_at_the_first_write_that_fails) {
    Collecting transport("second");
    const std::vector<std::string> payloads = {"first", "second", "third"};

    auto [written] = run(transport.write_messages(payloads));
    ZASSERT(written.has_error());
    ZEXPECT(written.error().message == "write failed");
    ZEXPECT(transport.written == std::vector<std::string>{"first"});
}

};  // ZEST_SUITE(ipc_transport)

}  // namespace

}  // namespace kota::ipc
