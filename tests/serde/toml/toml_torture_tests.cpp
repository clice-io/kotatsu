#if __has_include(<toml++/toml.hpp>)

#include <string>

#include "../standard_case_suite.h"
#include "eventide/zest/zest.h"
#include "eventide/serde/toml.h"

namespace eventide::serde {

namespace {

using toml::parse;
using toml::to_string;

auto rt = []<typename T>(const T& input) -> std::expected<T, toml::error_kind> {
    auto encoded = to_string(input);
    if(!encoded) {
        return std::unexpected(encoded.error());
    }
    return parse<T>(*encoded);
};

TEST_SUITE(serde_toml_standard) {

SERDE_STANDARD_TEST_CASES_PRIMITIVES(rt)
SERDE_STANDARD_TEST_CASES_NUMERIC_BOUNDARIES_TOML_SAFE(rt)
SERDE_STANDARD_TEST_CASES_TUPLE_LIKE(rt)
SERDE_STANDARD_TEST_CASES_SEQUENCE_SET(rt)
SERDE_STANDARD_TEST_CASES_MAPS(rt)
SERDE_STANDARD_TEST_CASES_OPTIONAL(rt)
SERDE_STANDARD_TEST_CASES_POINTERS_TOML_SAFE(rt)
SERDE_STANDARD_TEST_CASES_COMPLEX(rt)

};  // TEST_SUITE(serde_toml_standard)

}  // namespace

}  // namespace eventide::serde

#endif
