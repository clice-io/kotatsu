#include <string_view>
#include <utility>

#include "kota/http/detail/curl.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"

namespace kota::curl {

namespace {

ZEST_SUITE(http_detail_curl) {

ZEST_CASE(ok_holds_for_the_ok_codes_only) {
    EXPECT(ok(CURLE_OK));
    EXPECT(!ok(CURLE_COULDNT_CONNECT));
    EXPECT(ok(CURLM_OK));
    EXPECT(!ok(CURLM_BAD_HANDLE));
    EXPECT(ok(CURLSHE_OK));
    EXPECT(!ok(CURLSHE_IN_USE));
}

ZEST_CASE(to_easy_error_turns_failures_into_failed_init) {
    EXPECT(to_easy_error(CURLM_OK) == CURLE_OK);
    EXPECT(to_easy_error(CURLM_OUT_OF_MEMORY) == CURLE_FAILED_INIT);
    EXPECT(to_easy_error(CURLSHE_OK) == CURLE_OK);
    EXPECT(to_easy_error(CURLSHE_NOMEM) == CURLE_FAILED_INIT);
}

ZEST_CASE(message_is_curls_text) {
    EXPECT(message(CURLE_COULDNT_CONNECT) ==
           std::string_view(::curl_easy_strerror(CURLE_COULDNT_CONNECT)));
    EXPECT(message(CURLM_BAD_HANDLE) == std::string_view(::curl_multi_strerror(CURLM_BAD_HANDLE)));
    EXPECT(message(CURLSHE_IN_USE) == std::string_view(::curl_share_strerror(CURLSHE_IN_USE)));
}

ZEST_CASE(slist_keeps_lines_in_order) {
    slist lines;
    EXPECT(!lines);
    ASSERT(lines.append("first"));
    ASSERT(lines.append("second"));
    ASSERT(lines);

    auto* head = lines.get();
    EXPECT(std::string_view(head->data) == "first");
    ASSERT(head->next != nullptr);
    EXPECT(std::string_view(head->next->data) == "second");
    EXPECT((head->next->next == nullptr));
}

ZEST_CASE(slist_owns_a_list_it_is_given) {
    slist lines(::curl_slist_append(nullptr, "line"));
    ASSERT(lines);
    EXPECT(std::string_view(lines.get()->data) == "line");
}

ZEST_CASE(slist_moves_its_lines) {
    slist from;
    ASSERT(from.append("line"));
    auto* head = from.get();

    slist to(std::move(from));
    EXPECT(!from);
    EXPECT((to.get() == head));

    slist other;
    other = std::move(to);
    EXPECT(!to);
    EXPECT((other.get() == head));
    other.reset();
    EXPECT(!other);
}

};  // ZEST_SUITE(http_detail_curl)

}  // namespace

}  // namespace kota::curl
