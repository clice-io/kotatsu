#include <chrono>
#include <concepts>
#include <utility>

#include "kotatsu/http/http.h"
#include "kotatsu/zest/macro.h"
#include "kotatsu/zest/zest.h"

namespace kotatsu::http {

namespace {

using namespace std::chrono_literals;

ZEST_SUITE(http_detail_request_settings) {

// A chain on a named client or request goes on with it; one on a temporary
// moves it along, so that `auto c = http::client().header(...)` keeps it.
ZEST_CASE(setters_return_what_they_were_called_on) {
    ZSTATIC_EXPECT((std::same_as<decltype(std::declval<client&>().header("a", "b")), client&>));
    ZSTATIC_EXPECT((std::same_as<decltype(std::declval<client>().header("a", "b")), client&&>));
    ZSTATIC_EXPECT((std::same_as<decltype(std::declval<request&>().timeout(1s)), request&>));
    ZSTATIC_EXPECT((std::same_as<decltype(std::declval<request>().timeout(1s)), request&&>));
    ZSTATIC_EXPECT((
        std::same_as<decltype(std::declval<client&>().curl_option(CURLOPT_VERBOSE, 0L)), client&>));
}

};  // ZEST_SUITE(http_detail_request_settings)

}  // namespace

}  // namespace kotatsu::http
