#if __has_include(<flatbuffers/flatbuffers.h>)

#include <string>

#include "../roundtrip_suite.h"
#include "eventide/zest/zest.h"
#include "eventide/serde/flatbuffers/flatbuffers.h"

namespace eventide::serde {

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

TEST_SUITE(serde_flatbuffers_torture) {

TEST_CASE(ultimate_roundtrip){SERDE_TEST_ULTIMATE_ROUNDTRIP(rt)}

TEST_CASE(variant_and_nullables_roundtrip){SERDE_TEST_VARIANT_NULLABLES_ROUNDTRIP(rt)}

TEST_CASE(scalars_roundtrip){SERDE_TEST_SCALARS_ROUNDTRIP(rt)}

TEST_CASE(nested_containers_roundtrip){SERDE_TEST_NESTED_CONTAINERS_ROUNDTRIP(rt)}

TEST_CASE(empty_containers_roundtrip) {
    SERDE_TEST_EMPTY_CONTAINERS_ROUNDTRIP(rt)
}

};  // TEST_SUITE(serde_flatbuffers_torture)

}  // namespace

}  // namespace eventide::serde

#endif
