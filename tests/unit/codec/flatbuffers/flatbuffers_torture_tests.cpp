#if __has_include(<flatbuffers/flatbuffers.h>)


#include "../standard_case_suite.h"
#include "kota/zest/zest.h"
#include "kota/codec/flatbuffers/flatbuffers.h"

namespace kota::codec {

namespace {

auto rt = []<typename T>(const T& input) -> std::expected<T, flatbuffers::object_error_code> {
    auto encoded = flatbuffers::to_flatbuffer(input);
    if(!encoded) {
        return std::unexpected(encoded.error());
    }
    if(encoded->empty()) {
        return std::unexpected(flatbuffers::object_error_code::invalid_state);
    }
    return flatbuffers::from_flatbuffer<T>(*encoded);
};

TEST_SUITE(serde_flatbuffers_standard) {

SERDE_STANDARD_TEST_CASES_PRIMITIVES(rt)
SERDE_STANDARD_TEST_CASES_NUMERIC_BOUNDARIES(rt)
SERDE_STANDARD_TEST_CASES_TUPLE_LIKE(rt)
SERDE_STANDARD_TEST_CASES_SEQUENCE_SET(rt)
SERDE_STANDARD_TEST_CASES_MAPS(rt)
SERDE_STANDARD_TEST_CASES_OPTIONAL(rt)
SERDE_STANDARD_TEST_CASES_POINTERS_WIRE_SAFE(rt)
SERDE_STANDARD_TEST_CASES_COMPLEX(rt)

};  // TEST_SUITE(serde_flatbuffers_standard)

}  // namespace

}  // namespace kota::codec

#endif
