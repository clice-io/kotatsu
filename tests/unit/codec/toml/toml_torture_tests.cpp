#if __has_include(<toml++/toml.hpp>)

#include <string>

#include "../standard_case_suite.h"
#include "kota/zest/zest.h"
#include "kota/codec/toml/toml.h"

namespace kota::codec {

namespace {

using toml::from_string;
using toml::to_string;

auto rt = []<typename T>(const T& input) -> std::expected<T, rich_error> {
    auto encoded = to_string(input);
    if(!encoded) {
        return std::unexpected(rich_error(encoded.error().to_string()));
    }
    return from_string<T>(*encoded);
};

ZEST_SUITE(serde_toml_standard) {

SERDE_STANDARD_TEST_CASES_PRIMITIVES(rt)
SERDE_STANDARD_TEST_CASES_NUMERIC_BOUNDARIES_TOML_SAFE(rt)
SERDE_STANDARD_TEST_CASES_TUPLE_LIKE(rt)
SERDE_STANDARD_TEST_CASES_SEQUENCE_SET(rt)
SERDE_STANDARD_TEST_CASES_MAPS(rt)
SERDE_STANDARD_TEST_CASES_OPTIONAL(rt)
SERDE_STANDARD_TEST_CASES_POINTERS_TOML_SAFE(rt)
SERDE_STANDARD_TEST_CASES_COMPLEX(rt)

};  // ZEST_SUITE(serde_toml_standard)

}  // namespace

}  // namespace kota::codec

#endif
