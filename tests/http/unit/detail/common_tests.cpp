#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kota/http/detail/common.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"

namespace kota::http {

namespace {

ZEST_SUITE(http_detail_common) {

ZEST_CASE(curl_error_carries_its_code_and_curls_text) {
    auto err = error::from_curl(CURLE_COULDNT_CONNECT);
    EXPECT(err.kind == error_kind::curl);
    EXPECT(err.curl_code == CURLE_COULDNT_CONNECT);
    EXPECT(err.message() == std::string_view(::curl_easy_strerror(CURLE_COULDNT_CONNECT)));
}

ZEST_CASE(multi_and_share_failures_become_failed_init) {
    auto multi = error::from_curl(CURLM_OUT_OF_MEMORY, "multi");
    EXPECT(multi.kind == error_kind::curl);
    EXPECT(multi.curl_code == CURLE_FAILED_INIT);
    EXPECT(multi.message() == "multi");

    auto share = error::from_curl(CURLSHE_IN_USE, "share");
    EXPECT(share.kind == error_kind::curl);
    EXPECT(share.curl_code == CURLE_FAILED_INIT);
    EXPECT(share.message() == "share");
}

ZEST_CASE(message_is_the_detail_when_there_is_one) {
    const std::vector<std::pair<std::string_view, error>> errors{
        {"curl", error::from_curl(CURLE_COULDNT_CONNECT, "no route")},
        {"invalid_request", error::invalid_request("no route")},
        {"json_encode", error::json_encode("no route")},
        {"aborted", error::aborted("no route")},
    };
    for(const auto& [kind, err]: errors) {
        ZEST_CONTEXT("{}", kind);
        EXPECT(err.message() == "no route");
    }
}

ZEST_CASE(message_without_a_detail_names_the_kind) {
    EXPECT(error::invalid_request("").message() == "invalid http request");
    EXPECT(error::json_encode("").message() == "json encode failed");
    EXPECT(error::aborted("").message() == "http request aborted");
}

ZEST_CASE(message_function_matches_the_member) {
    auto err = error::aborted("gone");
    EXPECT(http::message(err) == err.message());
}

ZEST_CASE(redirect_policy_follows_ten_with_referer_by_default) {
    redirect_policy policy;
    EXPECT(policy.follow);
    EXPECT(policy.max_redirects == 10U);
    EXPECT(policy.referer);
}

ZEST_CASE(redirect_policy_none_follows_nothing) {
    auto policy = redirect_policy::none();
    EXPECT(!policy.follow);
    EXPECT(policy.max_redirects == 0U);
    EXPECT(!policy.referer);
}

ZEST_CASE(redirect_policy_limited_follows_up_to_its_count) {
    auto policy = redirect_policy::limited(3);
    EXPECT(policy.follow);
    EXPECT(policy.max_redirects == 3U);
    EXPECT(policy.referer);
}

};  // ZEST_SUITE(http_detail_common)

}  // namespace

}  // namespace kota::http
