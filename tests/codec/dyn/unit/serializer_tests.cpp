#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/codec/dyn/dyn.h"

namespace kota::codec {

namespace {

ZEST_SUITE(codec_dyn_serializer) {

ZEST_CASE(serialize_leaf_values) {
    auto null_r = dyn::to_dyn(nullptr);
    ASSERT(null_r);
    EXPECT(null_r->is_null());

    auto bool_r = dyn::to_dyn(true);
    ASSERT(bool_r);
    EXPECT(bool_r->as_bool() == true);

    auto int_r = dyn::to_dyn(42);
    ASSERT(int_r);
    EXPECT(int_r->as_int() == 42);

    auto str_r = dyn::to_dyn(std::string("hello"));
    ASSERT(str_r);
    EXPECT(str_r->as_string() == "hello");
}

ZEST_CASE(serialize_array) {
    std::vector<int> vec = {1, 2};
    auto result = dyn::to_dyn(vec);
    ASSERT(result);
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
    ASSERT(result);
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
    ASSERT(encoded);

    std::map<int, int> by_id;
    EXPECT(!dyn::from_dyn(*encoded, by_id).has_value());
}

ZEST_CASE(null_from_non_null_fails) {
    std::nullptr_t null = nullptr;
    auto status = dyn::from_dyn(dyn::Value(std::int64_t{42}), null);
    ASSERT(!status);
    EXPECT(status.error().message == "invalid type: expected null, got signed_int");

    // An untagged variant's last alternative decodes on the real reader when
    // nothing else claims the value; a null alternative there must not
    // swallow it.
    std::variant<int, std::monostate> choice = 1;
    auto fallback = dyn::from_dyn(dyn::Value(std::string("x")), choice);
    ASSERT(!fallback);
    EXPECT(fallback.error().message == "invalid type: expected null, got string");
}

ZEST_CASE(serialize_element_with_dom_subtree) {
    dyn::Object subtree;
    subtree.insert("k", dyn::Value(std::int64_t(9)));

    std::vector<dyn::Value> vec;
    vec.push_back(dyn::Value(std::move(subtree)));

    auto result = dyn::to_dyn(vec);
    ASSERT(result);
    ASSERT(result->is_array());
    const auto& array = result->as_array();
    ASSERT(array.size() == 1);
    ASSERT(array[0].is_object());
    EXPECT(array[0].as_object().at("k").as_int() == 9);
}

ZEST_CASE(codec_serialize_returns_dyn_value) {
    auto result = dyn::to_dyn(42);
    ASSERT(result);
    EXPECT(result->as_int() == 42);
}

ZEST_CASE(passthrough_value) {
    dyn::Value original(std::int64_t{99});
    auto result = dyn::to_dyn(original);
    ASSERT(result);
    EXPECT(result->as_int() == 99);
}

ZEST_CASE(passthrough_array) {
    dyn::Array arr;
    arr.push_back(dyn::Value(std::int64_t{1}));
    arr.push_back(dyn::Value(std::int64_t{2}));
    auto result = dyn::to_dyn(arr);
    ASSERT(result);
    ASSERT(result->is_array());
    EXPECT(result->as_array().size() == 2);
}

ZEST_CASE(passthrough_object) {
    dyn::Object obj;
    obj.insert("x", dyn::Value(std::int64_t{1}));
    auto result = dyn::to_dyn(obj);
    ASSERT(result);
    ASSERT(result->is_object());
    EXPECT(result->as_object().at("x").as_int() == 1);
}

};  // ZEST_SUITE(codec_dyn_serializer)

}  // namespace

}  // namespace kota::codec
