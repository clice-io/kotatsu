#include <string>
#include <unordered_set>

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

// std's hash of a std::variant keys the peer's maps: a number and the string
// of its digits are different ids.
ZEST_CASE(request_ids_key_an_unordered_set) {
    std::unordered_set<RequestID> ids{RequestID(1), RequestID(std::string("1"))};
    EXPECT(ids.size() == 2U);
    EXPECT(ids.contains(RequestID(1)));
    EXPECT(ids.contains(RequestID(std::string("1"))));
    EXPECT(!ids.contains(RequestID(2)));
}

};  // ZEST_SUITE(ipc_protocol)

}  // namespace

}  // namespace kota::ipc::protocol
