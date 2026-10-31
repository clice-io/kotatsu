#include "kotatsu/http/http.h"
#include "kotatsu/zest/async.h"
#include "kotatsu/zest/macro.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/async/async.h"

namespace kotatsu::http {

namespace {

ZEST_SUITE(http_detail_bound_client, zest::LoopFixture) {

ZEST_CASE(loop_is_the_one_it_was_bound_to) {
    async::event_loop other;
    http::client client;
    ZEXPECT((&client.on(loop).loop() == &loop));
    ZEXPECT((&client.on(other).loop() == &other));
}

ZEST_CASE(on_binds_the_running_loop_by_default) {
    http::client client;
    auto bind = [&]() -> async::task<async::event_loop*> {
        co_return &client.on().loop();
    };

    auto [bound] = run(bind());
    ZASSERT(bound.has_value());
    ZEXPECT((*bound == &loop));
}

};  // ZEST_SUITE(http_detail_bound_client)

}  // namespace

}  // namespace kotatsu::http
