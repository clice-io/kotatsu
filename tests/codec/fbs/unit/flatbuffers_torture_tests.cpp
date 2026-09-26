#if __has_include(<flatbuffers/flatbuffers.h>)

#include "codec/harness/standard_case_suite.h"
#include "kota/zest/zest.h"
#include "kota/codec/fbs/fbs.h"

namespace kota::codec {

namespace {

auto rt = []<typename T>(const T& input) -> std::expected<T, rich_error> {
    auto encoded = fbs::to_bytes(input);
    if(!encoded) {
        return std::unexpected(std::move(encoded.error()));
    }
    if(encoded->empty()) {
        return std::unexpected(rich_error("empty flatbuffer"));
    }
    return fbs::from_bytes<T>(*encoded);
};

ZEST_SUITE(codec_fbs_flatbuffers_torture) {

SERDE_STANDARD_TEST_CASES_PRIMITIVES(rt)
SERDE_STANDARD_TEST_CASES_NUMERIC_BOUNDARIES(rt)
SERDE_STANDARD_TEST_CASES_TUPLE_LIKE(rt)
SERDE_STANDARD_TEST_CASES_SEQUENCE_SET(rt)
SERDE_STANDARD_TEST_CASES_MAPS(rt)
SERDE_STANDARD_TEST_CASES_OPTIONAL(rt)
SERDE_STANDARD_TEST_CASES_POINTERS_FORMAT_SAFE(rt)
SERDE_STANDARD_TEST_CASES_VARIANT_FORMAT_SAFE(rt)
SERDE_STANDARD_TEST_CASES_TAGGED_VARIANTS(rt)
SERDE_STANDARD_TEST_CASES_COMPLEX(rt)

};  // ZEST_SUITE(codec_fbs_flatbuffers_torture)

}  // namespace

}  // namespace kota::codec

#endif
