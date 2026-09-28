#include "kota/ipc/protocol.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"

namespace kota::ipc::protocol {

namespace {

ZEST_SUITE(ipc_protocol) {

ZEST_CASE(error_from_a_null_c_string_has_no_message) {
    const char* message = nullptr;
    Error error(message);
    EXPECT(error.message.empty());
}

};  // ZEST_SUITE(ipc_protocol)

}  // namespace

}  // namespace kota::ipc::protocol
