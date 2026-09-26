#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/codec/dyn/dyn.h"

namespace kota::codec {

namespace {

ZEST_SUITE(serde_dyn_serializer){

    ZEST_CASE(serialize_leaf_values){auto null_r = dyn::to_dyn(nullptr);
ASSERT(null_r.has_value());
EXPECT(null_r->is_null());

auto bool_r = dyn::to_dyn(true);
ASSERT(bool_r.has_value());
EXPECT(bool_r->as_bool() == true);

auto int_r = dyn::to_dyn(42);
ASSERT(int_r.has_value());
EXPECT(int_r->as_int() == 42);

auto str_r = dyn::to_dyn(std::string("hello"));
ASSERT(str_r.has_value());
EXPECT(str_r->as_string() == "hello");

}  // namespace

ZEST_CASE(serialize_array) {
    std::vector<int> vec = {1, 2};
    auto result = dyn::to_dyn(vec);
    ASSERT(result.has_value());
    ASSERT(result->is_array());
    EXPECT(result->as_array().size() == 2);
    EXPECT(result->as_array()[0].as_int() == 1);
    EXPECT(result->as_array()[1].as_int() == 2);
}

ZEST_CASE(serialize_object) {
    std::map<std::string, int> m;
    m["a"] = 1;
    m["b"] = 2;
    auto result = dyn::to_dyn(m);
    ASSERT(result.has_value());
    ASSERT(result->is_object());

    const auto& obj = result->as_object();
    EXPECT(obj.size() == 2);
    EXPECT(obj.at("a").as_int() == 1);
    EXPECT(obj.at("b").as_int() == 2);
}

ZEST_CASE(map_key_parse_error) {
    std::map<std::string, int> src{
        {"abc", 1}
    };
    auto encoded = dyn::to_dyn(src);
    ASSERT(encoded.has_value());

    std::map<int, int> by_id;
    EXPECT(!dyn::from_dyn(*encoded, by_id).has_value());
}

ZEST_CASE(serialize_element_with_dom_subtree) {
    dyn::Object subtree;
    subtree.insert("k", dyn::Value(std::int64_t(9)));

    std::vector<dyn::Value> vec;
    vec.push_back(dyn::Value(std::move(subtree)));

    auto result = dyn::to_dyn(vec);
    ASSERT(result.has_value());
    ASSERT(result->is_array());
    const auto& array = result->as_array();
    ASSERT(array.size() == 1);
    ASSERT(array[0].is_object());
    EXPECT(array[0].as_object().at("k").as_int() == 9);
}

ZEST_CASE(codec_serialize_returns_dyn_value) {
    auto result = dyn::to_dyn(42);
    ASSERT(result.has_value());
    EXPECT(result->as_int() == 42);
}

ZEST_CASE(passthrough_value) {
    dyn::Value original(std::int64_t{99});
    auto result = dyn::to_dyn(original);
    ASSERT(result.has_value());
    EXPECT(result->as_int() == 99);
}

ZEST_CASE(passthrough_array) {
    dyn::Array arr;
    arr.push_back(dyn::Value(std::int64_t{1}));
    arr.push_back(dyn::Value(std::int64_t{2}));
    auto result = dyn::to_dyn(arr);
    ASSERT(result.has_value());
    ASSERT(result->is_array());
    EXPECT(result->as_array().size() == 2);
}

ZEST_CASE(passthrough_object) {
    dyn::Object obj;
    obj.insert("x", dyn::Value(std::int64_t{1}));
    auto result = dyn::to_dyn(obj);
    ASSERT(result.has_value());
    ASSERT(result->is_object());
    EXPECT(result->as_object().at("x").as_int() == 1);
}

};  // namespace kota::codec

}  // namespace

}  // namespace kota::codec
