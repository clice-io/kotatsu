#include <utility>

#include "kotatsu/http/detail/curl.h"
#include "kotatsu/zest/macro.h"
#include "kotatsu/zest/zest.h"

// The owning wrappers of curl's handles, over real handles: a multi handle
// opens a wakeup socket pair, so these are system tests.

namespace kotatsu::curl {

namespace {

/// Checks that moving `Handle`s passes the one handle along, leaving the
/// moved-from empty, and that reset() and release() let it go.
template <typename Handle>
void check_moves(Handle made) {
    ZASSERT(made);
    auto* raw = made.get();

    Handle constructed(std::move(made));
    ZEXPECT(!made);
    ZEXPECT((constructed.get() == raw));

    Handle assigned;
    assigned = std::move(constructed);
    ZEXPECT(!constructed);
    ZEXPECT((assigned.get() == raw));

    Handle released(assigned.release());
    ZEXPECT(!assigned);
    ZEXPECT((released.get() == raw));
    released.reset();
    ZEXPECT(!released);
}

ZEST_SUITE(http_detail_curl_handles) {

ZEST_CASE(easy_handle_passes_its_handle_along) {
    check_moves(easy_handle::create());
}

ZEST_CASE(multi_handle_passes_its_handle_along) {
    check_moves(multi_handle::create());
}

ZEST_CASE(share_handle_passes_its_handle_along) {
    check_moves(share_handle::create());
}

};  // ZEST_SUITE(http_detail_curl_handles)

}  // namespace

}  // namespace kotatsu::curl
