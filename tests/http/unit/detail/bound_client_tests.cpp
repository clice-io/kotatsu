#include "kota/http/http.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::http {

namespace {

ZEST_SUITE(http_detail_bound_client, zest::LoopFixture) {

ZEST_CASE(loop_is_the_one_it_was_bound_to) {
    event_loop other;
    http::client client;
    ZEXPECT((&client.on(loop).loop() == &loop));
    ZEXPECT((&client.on(other).loop() == &other));
}

ZEST_CASE(on_binds_the_running_loop_by_default) {
    http::client client;
    auto bind = [&]() -> task<event_loop*> {
        co_return &client.on().loop();
    };

    auto [bound] = run(bind());
    ZASSERT(bound.has_value());
    ZEXPECT((*bound == &loop));
}

};  // ZEST_SUITE(http_detail_bound_client)

}  // namespace

}  // namespace kota::http
