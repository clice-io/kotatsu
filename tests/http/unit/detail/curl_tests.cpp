#include <string_view>
#include <utility>

#include "kota/http/detail/curl.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"

namespace kota::curl {

namespace {

ZEST_SUITE(http_detail_curl) {

ZEST_CASE(ok_holds_for_the_ok_codes_only) {
    ZEXPECT(ok(CURLE_OK));
    ZEXPECT(!ok(CURLE_COULDNT_CONNECT));
    ZEXPECT(ok(CURLM_OK));
    ZEXPECT(!ok(CURLM_BAD_HANDLE));
    ZEXPECT(ok(CURLSHE_OK));
    ZEXPECT(!ok(CURLSHE_IN_USE));
}

ZEST_CASE(to_easy_error_turns_failures_into_failed_init) {
    ZEXPECT(to_easy_error(CURLM_OK) == CURLE_OK);
    ZEXPECT(to_easy_error(CURLM_OUT_OF_MEMORY) == CURLE_FAILED_INIT);
    ZEXPECT(to_easy_error(CURLSHE_OK) == CURLE_OK);
    ZEXPECT(to_easy_error(CURLSHE_NOMEM) == CURLE_FAILED_INIT);
}

ZEST_CASE(message_is_curls_text) {
    ZEXPECT(message(CURLE_COULDNT_CONNECT) ==
            std::string_view(::curl_easy_strerror(CURLE_COULDNT_CONNECT)));
    ZEXPECT(message(CURLM_BAD_HANDLE) == std::string_view(::curl_multi_strerror(CURLM_BAD_HANDLE)));
    ZEXPECT(message(CURLSHE_IN_USE) == std::string_view(::curl_share_strerror(CURLSHE_IN_USE)));
}

ZEST_CASE(slist_keeps_lines_in_order) {
    slist lines;
    ZEXPECT(!lines);
    ZASSERT(lines.append("first"));
    ZASSERT(lines.append("second"));
    ZASSERT(lines);

    auto* head = lines.get();
    ZEXPECT(std::string_view(head->data) == "first");
    ZASSERT(head->next != nullptr);
    ZEXPECT(std::string_view(head->next->data) == "second");
    ZEXPECT((head->next->next == nullptr));
}

ZEST_CASE(slist_owns_a_list_it_is_given) {
    slist lines(::curl_slist_append(nullptr, "line"));
    ZASSERT(lines);
    ZEXPECT(std::string_view(lines.get()->data) == "line");
}

ZEST_CASE(slist_moves_its_lines) {
    slist from;
    ZASSERT(from.append("line"));
    auto* head = from.get();

    slist to(std::move(from));
    ZEXPECT(!from);
    ZEXPECT((to.get() == head));

    slist other;
    other = std::move(to);
    ZEXPECT(!to);
    ZEXPECT((other.get() == head));
    other.reset();
    ZEXPECT(!other);
}

};  // ZEST_SUITE(http_detail_curl)

}  // namespace

}  // namespace kota::curl
