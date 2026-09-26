#include "codec/harness/standard_case_suite.h"
#include "kota/zest/zest.h"
#include "kota/codec/json/json.h"

namespace kota::codec {

namespace {

using json::from_string;
using json::to_string;

auto rt = []<typename T>(const T& input) -> std::expected<T, rich_error> {
    auto encoded = to_string(input);
    if(!encoded) {
        return std::unexpected(rich_error(encoded.error().to_string()));
    }
    return from_string<T>(*encoded);
};

ZEST_SUITE(codec_json_simdjson_torture) {

SERDE_STANDARD_TEST_CASES_PRIMITIVES(rt)
SERDE_STANDARD_TEST_CASES_NUMERIC_BOUNDARIES(rt)
SERDE_STANDARD_TEST_CASES_TUPLE_LIKE(rt)
SERDE_STANDARD_TEST_CASES_SEQUENCE_SET(rt)
SERDE_STANDARD_TEST_CASES_MAPS(rt)
SERDE_STANDARD_TEST_CASES_OPTIONAL(rt)
SERDE_STANDARD_TEST_CASES_POINTERS_FORMAT_SAFE(rt)
SERDE_STANDARD_TEST_CASES_VARIANT_FORMAT_SAFE(rt)
SERDE_STANDARD_TEST_CASES_ATTRS(rt)
SERDE_STANDARD_TEST_CASES_TAGGED_VARIANTS(rt)
SERDE_STANDARD_TEST_CASES_COMPLEX(rt)

};  // ZEST_SUITE(codec_json_simdjson_torture)

}  // namespace

}  // namespace kota::codec
