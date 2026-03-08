#include <string>

#include "../standard_case_suite.h"
#include "eventide/zest/zest.h"
#include "eventide/serde/json/deserializer.h"
#include "eventide/serde/json/serializer.h"

namespace eventide::serde {

namespace {

using json::from_json;
using json::to_json;

auto rt = []<typename T>(const T& input) -> std::expected<T, json::error_kind> {
    auto encoded = to_json(input);
    if(!encoded) {
        return std::unexpected(encoded.error());
    }
    return from_json<T>(*encoded);
};

TEST_SUITE(serde_simdjson_standard) {

SERDE_STANDARD_TEST_CASES_PRIMITIVES(rt)
SERDE_STANDARD_TEST_CASES_NUMERIC_BOUNDARIES(rt)
SERDE_STANDARD_TEST_CASES_TUPLE_LIKE(rt)
SERDE_STANDARD_TEST_CASES_SEQUENCE_SET(rt)
SERDE_STANDARD_TEST_CASES_MAPS(rt)
SERDE_STANDARD_TEST_CASES_OPTIONAL(rt)
SERDE_STANDARD_TEST_CASES_POINTERS_WIRE_SAFE(rt)
SERDE_STANDARD_TEST_CASES_VARIANT_WIRE_SAFE(rt)
SERDE_STANDARD_TEST_CASES_ATTRS(rt)
SERDE_STANDARD_TEST_CASES_TAGGED_VARIANTS(rt)
SERDE_STANDARD_TEST_CASES_COMPLEX(rt)

};  // TEST_SUITE(serde_simdjson_standard)

}  // namespace

}  // namespace eventide::serde
