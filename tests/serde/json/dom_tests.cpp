#include <string>

#include "eventide/zest/zest.h"
#include "eventide/serde/json/json.h"

namespace eventide::serde {

namespace {

struct mixed_payload {
    int id = 0;
    json::Value extra;
};

std::string make_large_object_json(int count) {
    std::string out = "{";
    for(int i = 0; i < count; ++i) {
        if(i > 0) {
            out.push_back(',');
        }
        out += "\"k";
        out += std::to_string(i);
        out += "\":";
        out += std::to_string(i);
    }
    out.push_back('}');
    return out;
}

TEST_SUITE(serde_json_dom) {

TEST_CASE(parse_and_view_basic) {
    auto node = json::Value::parse(R"({"a":1,"b":"x","arr":[1,2]})");
    ASSERT_TRUE(node.has_value());

    auto root = node->as_ref();
    auto object = root.get_object();
    ASSERT_TRUE(object.has_value());
    ASSERT_EQ((*object)["a"].as_int(), 1);
    ASSERT_EQ((*object)["b"].as_string(), std::string_view("x"));

    auto array = (*object)["arr"].get_array();
    ASSERT_TRUE(array.has_value());
    ASSERT_EQ((*array)[1].as_int(), 2);
    EXPECT_FALSE((*object).get("missing").has_value());
}

TEST_CASE(object_lookup_lazy_index_threshold) {
    auto json_text = make_large_object_json(32);
    auto node = json::Value::parse(json_text);
    ASSERT_TRUE(node.has_value());

    auto object = node->as_ref().get_object();
    ASSERT_TRUE(object.has_value());
    for(int i = 0; i < 32; ++i) {
        std::string key = "k" + std::to_string(i);
        ASSERT_EQ((*object)[key].as_int(), i);
    }
}

TEST_CASE(cow_mutation_preserves_copy) {
    auto node = json::Value::parse(R"({"n":1})");
    ASSERT_TRUE(node.has_value());

    auto copy = *node;
    auto node_object = node->as_object();
    ASSERT_TRUE(node_object.assign("n", 2).has_value());

    auto copy_object = copy.as_ref().get_object();
    ASSERT_TRUE(copy_object.has_value());
    EXPECT_EQ(node_object["n"].as_int(), 2);
    EXPECT_EQ((*copy_object)["n"].as_int(), 1);
}

TEST_CASE(mixed_struct_roundtrip_with_dynamic_dom) {
    auto parsed = json::parse<mixed_payload>(R"({"id":7,"extra":{"name":"alice","n":1}})");
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->id, 7);
    auto extra_object = parsed->extra.as_object();
    ASSERT_EQ(extra_object["name"].as_string(), std::string_view("alice"));
    ASSERT_EQ(extra_object["n"].as_int(), 1);

    ASSERT_TRUE(extra_object.assign("n", 2).has_value());
    parsed->extra = extra_object.as_value();

    auto encoded = json::to_string(*parsed);
    ASSERT_TRUE(encoded.has_value());

    auto reparsed = json::parse<mixed_payload>(*encoded);
    ASSERT_TRUE(reparsed.has_value());
    EXPECT_EQ(reparsed->id, 7);
    auto reparsed_extra_object = reparsed->extra.as_ref().get_object();
    ASSERT_TRUE(reparsed_extra_object.has_value());
    EXPECT_EQ((*reparsed_extra_object)["name"].as_string(), std::string_view("alice"));
    EXPECT_EQ((*reparsed_extra_object)["n"].as_int(), 2);
}

};  // TEST_SUITE(serde_json_dom)

}  // namespace

}  // namespace eventide::serde
