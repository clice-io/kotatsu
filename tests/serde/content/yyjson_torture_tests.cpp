#include <string>

#include "../standard_case_suite.h"
#include "eventide/zest/zest.h"
#include "eventide/serde/json/error.h"
#include "eventide/serde/json/json.h"
#include "eventide/serde/serde/serde.h"

namespace eventide::serde {

namespace {

using json::yy::Deserializer;
using json::yy::to_json;

auto rt = []<typename T>(const T& input) -> std::expected<T, json::error_kind> {
    auto encoded = to_json(input);
    if(!encoded) {
        return std::unexpected(encoded.error());
    }

    auto dom = json::Value::parse(*encoded);
    if(!dom) {
        return std::unexpected(json::make_read_error(dom.error()));
    }

    T decoded{};
    Deserializer deserializer(*dom);
    auto status = serde::deserialize(deserializer, decoded);
    if(!status) {
        return std::unexpected(status.error());
    }

    auto finish = deserializer.finish();
    if(!finish) {
        return std::unexpected(finish.error());
    }
    return decoded;
};

TEST_SUITE(serde_yyjson_standard) {

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

};  // TEST_SUITE(serde_yyjson_standard)

}  // namespace

}  // namespace eventide::serde
