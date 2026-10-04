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
    ZEXPECT(err.kind == error_kind::curl);
    ZEXPECT(err.curl_code == CURLE_COULDNT_CONNECT);
    ZEXPECT(err.message() == std::string_view(::curl_easy_strerror(CURLE_COULDNT_CONNECT)));
}

ZEST_CASE(multi_and_share_failures_become_failed_init) {
    auto multi = error::from_curl(CURLM_OUT_OF_MEMORY, "multi");
    ZEXPECT(multi.kind == error_kind::curl);
    ZEXPECT(multi.curl_code == CURLE_FAILED_INIT);
    ZEXPECT(multi.message() == "multi");

    auto share = error::from_curl(CURLSHE_IN_USE, "share");
    ZEXPECT(share.kind == error_kind::curl);
    ZEXPECT(share.curl_code == CURLE_FAILED_INIT);
    ZEXPECT(share.message() == "share");
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
        ZEXPECT(err.message() == "no route");
    }
}

ZEST_CASE(message_without_a_detail_names_the_kind) {
    ZEXPECT(error::invalid_request("").message() == "invalid http request");
    ZEXPECT(error::json_encode("").message() == "json encode failed");
    ZEXPECT(error::aborted("").message() == "http request aborted");
}

ZEST_CASE(message_function_matches_the_member) {
    auto err = error::aborted("gone");
    ZEXPECT(http::message(err) == err.message());
}

ZEST_CASE(redirect_policy_follows_ten_with_referer_by_default) {
    redirect_policy policy;
    ZEXPECT(policy.follow);
    ZEXPECT(policy.max_redirects == 10U);
    ZEXPECT(policy.referer);
}

ZEST_CASE(redirect_policy_none_follows_nothing) {
    auto policy = redirect_policy::none();
    ZEXPECT(!policy.follow);
    ZEXPECT(policy.max_redirects == 0U);
    ZEXPECT(!policy.referer);
}

ZEST_CASE(redirect_policy_limited_follows_up_to_its_count) {
    auto policy = redirect_policy::limited(3);
    ZEXPECT(policy.follow);
    ZEXPECT(policy.max_redirects == 3U);
    ZEXPECT(policy.referer);
}

};  // ZEST_SUITE(http_detail_common)

}  // namespace

}  // namespace kota::http
