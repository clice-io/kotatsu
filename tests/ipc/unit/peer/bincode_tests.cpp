#include "ipc/harness/codec_bincode.h"
#include "ipc/harness/peer_fixture.h"
#include "ipc/harness/peer_suite/cancel.h"
#include "ipc/harness/peer_suite/dispatch.h"
#include "ipc/harness/peer_suite/ending.h"
#include "ipc/harness/peer_suite/incoming.h"
#include "ipc/harness/peer_suite/lifecycle.h"
#include "ipc/harness/peer_suite/limit.h"
#include "ipc/harness/peer_suite/link.h"
#include "ipc/harness/peer_suite/requests.h"
#include "ipc/harness/peer_suite/timeout.h"
#include "ipc/harness/peer_suite/unreadable.h"
#include "kotatsu/ipc/peer/bincode.h"
#include "kotatsu/zest/macro.h"
#include "kotatsu/zest/zest.h"

namespace kotatsu::ipc {

namespace {

using Kit = test::PeerKit<test::BincodeAdapter>;

ZEST_SUITE(ipc_peer_bincode) {

ZEST_CASE_GROUP(dispatch) {
    test::peer_dispatch(Kit{add_case});
}

ZEST_CASE_GROUP(requests) {
    test::peer_requests(Kit{add_case});
}

ZEST_CASE_GROUP(cancel) {
    test::peer_cancel(Kit{add_case});
}

ZEST_CASE_GROUP(incoming) {
    test::peer_incoming(Kit{add_case});
}

ZEST_CASE_GROUP(timeout) {
    test::peer_timeout(Kit{add_case});
}

ZEST_CASE_GROUP(lifecycle) {
    test::peer_lifecycle(Kit{add_case});
}

ZEST_CASE_GROUP(ending) {
    test::peer_ending(Kit{add_case});
}

ZEST_CASE_GROUP(limit) {
    test::peer_limit(Kit{add_case});
}

ZEST_CASE_GROUP(link) {
    test::peer_link(Kit{add_case});
}

ZEST_CASE_GROUP(unreadable) {
    test::peer_unreadable(Kit{add_case});
}

ZEST_CASE(error_data_crosses_between_peers) {
    test::error_data_crosses_between_peers<test::BincodeAdapter>();
}

};  // ZEST_SUITE(ipc_peer_bincode)

}  // namespace

}  // namespace kotatsu::ipc
