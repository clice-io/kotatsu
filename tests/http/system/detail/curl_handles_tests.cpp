#include <utility>

#include "kota/http/detail/curl.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"

// The owning wrappers of curl's handles, over real handles: a multi handle
// opens a wakeup socket pair, so these are system tests.

namespace kota::curl {

namespace {

/// Checks that moving `Handle`s passes the one handle along, leaving the
/// moved-from empty, and that reset() and release() let it go.
template <typename Handle>
void check_moves(Handle made) {
    ASSERT(made);
    auto* raw = made.get();

    Handle constructed(std::move(made));
    EXPECT(!made);
    EXPECT((constructed.get() == raw));

    Handle assigned;
    assigned = std::move(constructed);
    EXPECT(!constructed);
    EXPECT((assigned.get() == raw));

    Handle released(assigned.release());
    EXPECT(!assigned);
    EXPECT((released.get() == raw));
    released.reset();
    EXPECT(!released);
}

ZEST_SUITE(http_detail_curl_handles) {

ZEST_CASE(easy_handle_passes_its_handle_along) {
    ZEST_CONTEXT("easy");
    check_moves(easy_handle::create());
}

ZEST_CASE(multi_handle_passes_its_handle_along) {
    ZEST_CONTEXT("multi");
    check_moves(multi_handle::create());
}

ZEST_CASE(share_handle_passes_its_handle_along) {
    ZEST_CONTEXT("share");
    check_moves(share_handle::create());
}

};  // ZEST_SUITE(http_detail_curl_handles)

}  // namespace

}  // namespace kota::curl
