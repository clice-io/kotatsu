#include <cstdint>
#include <string>
#include <string_view>

#include "kota/zest/zest.h"
#include "kota/codec/content.h"
#include "kota/codec/json/json.h"

namespace kota::codec {

namespace {

struct mixed_payload {
    int id = 0;
    json::Value extra;
};

struct dom_payload {
    int id = 0;
    std::string name;

    auto operator==(const dom_payload&) const -> bool = default;
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

TEST_SUITE(serde_content_dom) {

TEST_CASE(construct_scalars) {
    content::Value null_value{};
    EXPECT_TRUE(null_value.is_null());

    content::Value bool_value(true);
    EXPECT_TRUE(bool_value.is_bool());
    EXPECT_EQ(bool_value.as_bool(), true);

    content::Value int_value(std::int64_t(-7));
    EXPECT_TRUE(int_value.is_int());
    EXPECT_EQ(int_value.as_int(), -7);

    content::Value uint_value(std::uint64_t(42));
    EXPECT_TRUE(uint_value.is_int());
    EXPECT_EQ(uint_value.as_uint(), std::uint64_t(42));

    content::Value double_value(3.5);
    EXPECT_TRUE(double_value.is_number());
    EXPECT_EQ(double_value.as_double(), 3.5);

    content::Value string_value(std::string("hello"));
    EXPECT_TRUE(string_value.is_string());
    EXPECT_EQ(string_value.as_string(), std::string_view("hello"));
}

TEST_CASE(int_uint_cross_sign_access) {
    content::Value big_uint(std::uint64_t{9223372036854775808ULL});
    EXPECT_FALSE(big_uint.get_int().has_value());
    ASSERT_TRUE(big_uint.get_uint().has_value());
    EXPECT_EQ(*big_uint.get_uint(), std::uint64_t{9223372036854775808ULL});

    content::Value neg_int(std::int64_t{-1});
    EXPECT_FALSE(neg_int.get_uint().has_value());
    ASSERT_TRUE(neg_int.get_int().has_value());
    EXPECT_EQ(*neg_int.get_int(), std::int64_t{-1});
}

TEST_CASE(parse_and_view_basic_via_json) {
    auto parsed = json::parse<json::Value>(R"({"a":1,"b":"x","arr":[1,2]})");
    ASSERT_TRUE(parsed.has_value());

    auto root = parsed->as_ref();
    auto object = root.get_object();
    ASSERT_TRUE(object.valid());
    ASSERT_EQ(object.at("a").as_int(), 1);
    ASSERT_EQ(object.at("b").as_string(), std::string_view("x"));

    auto array = object["arr"].get_array();
    ASSERT_TRUE(array.valid());
    ASSERT_EQ(array[1].as_int(), 2);
    EXPECT_FALSE(object.contains("missing"));
}

TEST_CASE(object_lookup_builds_lazy_index) {
    auto json_text = make_large_object_json(32);
    auto parsed = json::parse<json::Value>(json_text);
    ASSERT_TRUE(parsed.has_value());

    auto object = parsed->as_ref().get_object();
    ASSERT_TRUE(object.valid());
    for(int i = 0; i < 32; ++i) {
        std::string key = "k" + std::to_string(i);
        ASSERT_EQ(object[key].as_int(), i);
    }
}

TEST_CASE(value_copy_is_deep) {
    content::Object obj;
    obj.insert("n", content::Value(std::int64_t(1)));

    content::Value original(std::move(obj));
    content::Value copy = original;

    original.as_object().assign("n", content::Value(std::int64_t(2)));

    EXPECT_EQ(original.as_object().at("n").as_int(), 2);
    EXPECT_EQ(copy.as_object().at("n").as_int(), 1);
}

TEST_CASE(object_equality_is_order_insensitive) {
    content::Object a;
    a.insert("x", content::Value(std::int64_t(1)));
    a.insert("y", content::Value(std::int64_t(2)));

    content::Object b;
    b.insert("y", content::Value(std::int64_t(2)));
    b.insert("x", content::Value(std::int64_t(1)));

    EXPECT_TRUE(a == b);
}

TEST_CASE(mixed_struct_roundtrip_with_dynamic_dom) {
    auto parsed = json::parse<mixed_payload>(R"({"id":7,"extra":{"name":"alice","n":1}})");
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->id, 7);

    auto& extra_object = parsed->extra.as_object();
    EXPECT_EQ(extra_object.at("name").as_string(), std::string_view("alice"));
    EXPECT_EQ(extra_object.at("n").as_int(), 1);

    extra_object.assign("n", content::Value(std::int64_t(2)));

    auto encoded = json::to_string(*parsed);
    ASSERT_TRUE(encoded.has_value());

    auto reparsed = json::parse<mixed_payload>(*encoded);
    ASSERT_TRUE(reparsed.has_value());
    EXPECT_EQ(reparsed->id, 7);
    auto reparsed_extra_object = reparsed->extra.as_ref().get_object();
    ASSERT_TRUE(reparsed_extra_object.valid());
    EXPECT_EQ(reparsed_extra_object["name"].as_string(), std::string_view("alice"));
    EXPECT_EQ(reparsed_extra_object["n"].as_int(), 2);
}

TEST_CASE(deep_nested_array_via_json_roundtrip) {
    constexpr int depth = 16;
    std::string text(depth, '[');
    text.push_back('1');
    text.append(depth, ']');

    auto parsed = json::parse<json::Value>(text);
    ASSERT_TRUE(parsed.has_value());

    content::ValueRef cursor = parsed->as_ref();
    for(int i = 0; i < depth; ++i) {
        auto array = cursor.get_array();
        ASSERT_TRUE(array.valid());
        ASSERT_EQ(array.size(), std::size_t(1));
        cursor = array[0];
    }
    ASSERT_TRUE(cursor.is_int());
    EXPECT_EQ(cursor.as_int(), 1);

    auto encoded = json::to_string(*parsed);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, text);
}

TEST_CASE(content_deserializer_keeps_temporary_root_value_alive) {
    auto make_dom = []() -> json::Value {
        auto parsed = json::parse<json::Value>(R"({"id":7,"name":"alice"})");
        return parsed ? std::move(*parsed) : json::Value{};
    };

    dom_payload payload{};
    content::Deserializer deserializer(make_dom());
    ASSERT_TRUE(deserializer.valid());

    auto status = codec::deserialize(deserializer, payload);
    ASSERT_TRUE(status.has_value());

    auto finish = deserializer.finish();
    ASSERT_TRUE(finish.has_value());
    EXPECT_EQ(payload, (dom_payload{.id = 7, .name = "alice"}));
}

};  // TEST_SUITE(serde_content_dom)

}  // namespace

}  // namespace kota::codec
