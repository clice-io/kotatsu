#include "ipc/harness/codec_bincode.h"
#include "ipc/harness/peer_fixture.h"
#include "ipc/harness/peer_suite/cancel.h"
#include "ipc/harness/peer_suite/dispatch.h"
#include "ipc/harness/peer_suite/lifecycle.h"
#include "ipc/harness/peer_suite/link.h"
#include "ipc/harness/peer_suite/requests.h"
#include "ipc/harness/peer_suite/timeout.h"
#include "kota/ipc/codec/bincode.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"

namespace kota::ipc {

namespace {

using Kit = test::PeerKit<test::BincodeWire>;

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

ZEST_CASE_GROUP(timeout) {
    test::peer_timeout(Kit{add_case});
}

ZEST_CASE_GROUP(lifecycle) {
    test::peer_lifecycle(Kit{add_case});
}

ZEST_CASE_GROUP(link) {
    test::peer_link(Kit{add_case});
}

ZEST_CASE(error_response_without_an_id_is_not_answered) {
    test::error_response_without_an_id_is_not_answered<test::BincodeWire>();
}

// BincodeCodec encodes the RawValue as a length-prefixed blob, so the
// requester gets the result wrapped once more.
ZEST_CASE(raw_value_result_is_sent_as_it_is, skip = true) {
    test::raw_value_result_is_sent_as_it_is<test::BincodeWire>();
}

// N5: empty params decode as a default AddParams, and the handler runs.
ZEST_CASE(request_without_params_is_answered_with_invalid_params, skip = true) {
    test::request_without_params_is_answered_with_invalid_params<test::BincodeWire>();
}

// P1.5: BincodeCodec drops Error::data.
ZEST_CASE(error_data_crosses_between_peers, skip = true) {
    test::error_data_crosses_between_peers<test::BincodeWire>();
}

// P1.2: the reply to an unparsable message carries id 0 instead of none.
ZEST_CASE(unparsable_message_is_answered_without_an_id, skip = true) {
    test::unparsable_message_is_answered_without_an_id<test::BincodeWire>();
}

// N1: a request sent after the input ended waits for an answer for good.
ZEST_CASE(request_from_a_handler_after_end_of_input_fails, skip = true) {
    test::request_from_a_handler_after_end_of_input_fails<test::BincodeWire>();
}

// N4: close_output() closes at once, so the queued message fails to write.
ZEST_CASE(close_output_writes_queued_messages_first, skip = true) {
    test::close_output_writes_queued_messages_first<test::BincodeWire>();
}

// N4: a send after close_output() is queued, fails to write, and the failure
// closes the input too.
ZEST_CASE(send_after_close_output_fails, skip = true) {
    test::send_after_close_output_fails<test::BincodeWire>();
}

};  // ZEST_SUITE(ipc_peer_bincode)

}  // namespace

}  // namespace kota::ipc
