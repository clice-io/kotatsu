#include "codec/harness/standard_case_suite.h"
#include "kota/zest/zest.h"
#include "kota/codec/dyn/dyn.h"

namespace kota::codec {

namespace {

using dyn::from_dyn;
using dyn::to_dyn;

auto rt = []<typename T>(const T& input) -> std::expected<T, rich_error> {
    auto encoded = to_dyn(input);
    if(!encoded) {
        return std::unexpected(rich_error(encoded.error().to_string()));
    }
    return from_dyn<T>(*encoded);
};

ZEST_SUITE(codec_dyn_torture) {

SERDE_STANDARD_TEST_CASES_PRIMITIVES(rt)
SERDE_STANDARD_TEST_CASES_NUMERIC_BOUNDARIES(rt)
SERDE_STANDARD_TEST_CASES_TUPLE_LIKE(rt)
SERDE_STANDARD_TEST_CASES_SEQUENCE_SET(rt)
SERDE_STANDARD_TEST_CASES_MAPS(rt)
SERDE_STANDARD_TEST_CASES_OPTIONAL(rt)
SERDE_STANDARD_TEST_CASES_POINTERS_FORMAT_SAFE(rt)
SERDE_STANDARD_TEST_CASES_VARIANT_FORMAT_SAFE(rt)
SERDE_STANDARD_TEST_CASES_COMPLEX(rt)

};  // ZEST_SUITE(codec_dyn_torture)

}  // namespace

}  // namespace kota::codec
