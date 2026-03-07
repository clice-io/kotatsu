#include <string>

#include "../roundtrip_suite.h"
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

TEST_SUITE(serde_yyjson_torture) {

TEST_CASE(ultimate_roundtrip){
    SERDE_TEST_ULTIMATE_ROUNDTRIP(rt)} TEST_CASE(variant_and_nullables_roundtrip){
    SERDE_TEST_VARIANT_NULLABLES_ROUNDTRIP(rt)} TEST_CASE(scalars_roundtrip){
    SERDE_TEST_SCALARS_ROUNDTRIP(rt)} TEST_CASE(nested_containers_roundtrip){
    SERDE_TEST_NESTED_CONTAINERS_ROUNDTRIP(rt)} TEST_CASE(empty_containers_roundtrip) {
    SERDE_TEST_EMPTY_CONTAINERS_ROUNDTRIP(rt)
}

};  // TEST_SUITE(serde_yyjson_torture)

}  // namespace

}  // namespace eventide::serde
