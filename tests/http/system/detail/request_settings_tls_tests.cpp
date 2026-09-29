#include <cstddef>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "http/harness/server.h"
#include "kota/http/http.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

// No TLS server runs here: these check what TLS settings do to plain http.

namespace kota::http {

namespace {

ZEST_SUITE(http_detail_request_settings_tls, test::LoopFixture) {

ZEST_CASE(https_only_refuses_plain_http) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = http::client().https_only();

    auto [reply] = run(client.on(loop).get(server.url("/")).send());
    ASSERT(reply.has_error());
    EXPECT(reply.error().kind == error_kind::curl);
    EXPECT(reply.error().curl_code == CURLE_UNSUPPORTED_PROTOCOL);
    EXPECT(server.requests().empty());
}

// curl takes each of them as it is set, so each is set once here.
ZEST_CASE(tls_settings_leave_plain_http_alone) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    http::client client;
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

// Schannel, the TLS of curl on Windows, keeps no CA directories.
ZEST_CASE(ca_path_leaves_plain_http_alone_where_curl_has_it) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    http::client client;

    auto [reply] = run(client.on(loop).get(server.url("/")).ca_path("kotatsu-missing-ca").send());
#ifdef _WIN32
    ASSERT(reply.has_error());
    EXPECT(reply.error().curl_code == CURLE_NOT_BUILT_IN);
#else
    EXPECT(reply.has_value());
#endif
}

};  // ZEST_SUITE(http_detail_request_settings_tls)

}  // namespace

}  // namespace kota::http
