#include "kota/http/http.h"
#include "kota/zest/zest.h"
#include "kota/async/io/loop.h"

ZEST_SUITE(http_manual_request) {

ZEST_SUITE_ATTRS(skip = true);

ZEST_CASE(get_request) {
    using namespace kota;
    event_loop loop;
    http::client client;
    auto request = client.on(loop).get("https://github.com").send();
    loop.schedule(request);
    loop.run();
    auto result = request.result();
    ASSERT(result);
    EXPECT(result->status == 200);
};

};  // ZEST_SUITE(http_manual_request)
