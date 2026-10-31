#include "kotatsu/ipc/protocol.h"
#include "kotatsu/zest/macro.h"
#include "kotatsu/zest/zest.h"

namespace kotatsu::ipc::protocol {

namespace {

ZEST_SUITE(ipc_protocol) {

ZEST_CASE(error_from_a_null_c_string_has_no_message) {
    const char* message = nullptr;
    Error error(message);
    ZEXPECT(error.message.empty());
}

};  // ZEST_SUITE(ipc_protocol)

}  // namespace

}  // namespace kotatsu::ipc::protocol
