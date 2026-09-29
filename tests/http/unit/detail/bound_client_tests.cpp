#include "async/harness/loop_fixture.h"
#include "kota/http/http.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::http {

namespace {

ZEST_SUITE(http_detail_bound_client, test::LoopFixture) {

ZEST_CASE(loop_is_the_one_it_was_bound_to) {
    event_loop other;
    http::client client;
    EXPECT((&client.on(loop).loop() == &loop));
    EXPECT((&client.on(other).loop() == &other));
}

ZEST_CASE(on_binds_the_running_loop_by_default) {
    http::client client;
    auto bind = [&]() -> task<event_loop*> {
        co_return &client.on().loop();
    };

    auto [bound] = run(bind());
    ASSERT(bound.has_value());
    EXPECT((*bound == &loop));
}

};  // ZEST_SUITE(http_detail_bound_client)

}  // namespace

}  // namespace kota::http
