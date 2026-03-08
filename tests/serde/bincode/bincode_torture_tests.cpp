#include <string>

#include "../standard_case_suite.h"
#include "eventide/zest/zest.h"
#include "eventide/serde/bincode.h"

namespace eventide::serde {

namespace {

using bincode::from_bytes;
using bincode::to_bytes;

auto rt = []<typename T>(const T& input) -> std::expected<T, bincode::error_kind> {
    auto encoded = to_bytes(input);
    if(!encoded) {
        return std::unexpected(encoded.error());
    }
    if(encoded->empty()) {
        return std::unexpected(bincode::error_kind::invalid_state);
    }
    return from_bytes<T>(*encoded);
};

TEST_SUITE(serde_bincode_standard) {

SERDE_STANDARD_TEST_CASES_PRIMITIVES(rt)
SERDE_STANDARD_TEST_CASES_NUMERIC_BOUNDARIES(rt)
SERDE_STANDARD_TEST_CASES_TUPLE_LIKE(rt)
SERDE_STANDARD_TEST_CASES_SEQUENCE_SET(rt)
SERDE_STANDARD_TEST_CASES_MAPS(rt)
SERDE_STANDARD_TEST_CASES_OPTIONAL(rt)
SERDE_STANDARD_TEST_CASES_POINTERS(rt)
SERDE_STANDARD_TEST_CASES_VARIANT(rt)
SERDE_STANDARD_TEST_CASES_COMPLEX(rt)

};  // TEST_SUITE(serde_bincode_standard)

}  // namespace

}  // namespace eventide::serde
