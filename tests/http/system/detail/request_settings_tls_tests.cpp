#include <cstddef>
#include <vector>

#include "http/harness/server.h"
#include "kota/http/detail/curl.h"
#include "kota/http/detail/manager.h"
#include "kota/http/http.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

// No TLS server runs here: these check what TLS settings do to plain http.

namespace kota::http {

namespace {

/// Whether curl's TLS takes a CA directory, which Schannel and Secure
/// Transport do not.
bool curl_takes_ca_path() {
    auto probe = curl::easy_handle::create();
    return curl::setopt(probe.get(), CURLOPT_CAPATH, "kotatsu") != CURLE_NOT_BUILT_IN;
}

ZEST_SUITE(http_detail_request_settings_tls, zest::LoopFixture) {

ZEST_CASE(plain_http_under_https_only_fails) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client().https_only();

    auto [reply] = run(client.on(loop).get(server.url("/")).send());
    ASSERT(reply.has_error());
    EXPECT(reply.error().kind == error_kind::curl);
    EXPECT(reply.error().curl_code == CURLE_UNSUPPORTED_PROTOCOL);
    EXPECT(server.requests().empty());
    EXPECT(manager::for_loop(loop).pending_requests() == 0U);
}

// curl checks each of these values when it is set, so the case sets
// each once.
ZEST_CASE(tls_settings_leave_plain_http_alone) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client();
    auto api = client.on(loop);
    auto url = server.url("/");
    std::vector<http::request> built{
        api.get(url).min_tls_version(tls_version::tls1_2).max_tls_version(tls_version::tls1_3),
        api.get(url).danger_accept_invalid_certs().danger_accept_invalid_hostnames(),
        api.get(url).ca_file("kotatsu-missing-ca.pem"),
    };
    for(auto version:
        {tls_version::tls1_0, tls_version::tls1_1, tls_version::tls1_2, tls_version::tls1_3}) {
        built.push_back(api.get(url).min_tls_version(version));
        built.push_back(api.get(url).max_tls_version(version));
    }

    for(std::size_t i = 0; i < built.size(); ++i) {
        ZEST_CONTEXT("request {}", i);
        auto [reply] = run(built[i].send());
        EXPECT(reply.has_value());
    }
    EXPECT(server.requests().size() == built.size());
}

// A TLS that keeps no CA directories refuses the option, and the request
// fails with curl's error; any other leaves plain http alone.
ZEST_CASE(ca_path_goes_to_curl) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).get(server.url("/")).ca_path("kotatsu-missing-ca").send());
    if(curl_takes_ca_path()) {
        EXPECT(reply.has_value());
    } else {
        ASSERT(reply.has_error());
        EXPECT(reply.error().curl_code == CURLE_NOT_BUILT_IN);
    }
}

};  // ZEST_SUITE(http_detail_request_settings_tls)

}  // namespace

}  // namespace kota::http
