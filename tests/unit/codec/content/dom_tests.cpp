#include <cstdint>
#include <string>

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

    content::Value string_value("hello");
    EXPECT_TRUE(string_value.is_string());
    EXPECT_EQ(string_value.as_string(), "hello");
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

    ASSERT_TRUE(parsed->is_object());
    ASSERT_EQ((*parsed)["a"].as_int(), 1);
    ASSERT_EQ((*parsed)["b"].as_string(), "x");
    ASSERT_EQ((*parsed)["arr"][1].as_int(), 2);
    EXPECT_FALSE((*parsed)["missing"].valid());
}

TEST_CASE(cursor_miss_describes_failure) {
    auto parsed = json::parse<json::Value>(R"({"a":{"b":[10,20]}})");
    ASSERT_TRUE(parsed.has_value());

    auto missing_key = (*parsed)["zzz"];
    EXPECT_FALSE(missing_key.valid());
    EXPECT_TRUE(missing_key.has_error());
    EXPECT_EQ(missing_key.error(), R"(missing key "zzz")");

    auto out_of_range = (*parsed)["a"]["b"][5];
    EXPECT_FALSE(out_of_range.valid());
    EXPECT_EQ(out_of_range.error(), "index 5 out of range (size 2)");

    auto wrong_kind = (*parsed)["a"]["b"]["x"];
    EXPECT_FALSE(wrong_kind.valid());
    EXPECT_EQ(wrong_kind.error(), "expected object, got array");
}

TEST_CASE(cursor_chain_appends_path) {
    auto parsed = json::parse<json::Value>(R"({"a":1})");
    ASSERT_TRUE(parsed.has_value());

    auto deep = (*parsed)["missing"]["x"][3]["y"];
    ASSERT_FALSE(deep.valid());
    EXPECT_EQ(deep.error(), R"(missing key "missing" -> ["x"] -> [3] -> ["y"])");
}

TEST_CASE(object_lookup_builds_lazy_index) {
    auto json_text = make_large_object_json(32);
    auto parsed = json::parse<json::Value>(json_text);
    ASSERT_TRUE(parsed.has_value());

    ASSERT_TRUE(parsed->is_object());
    for(int i = 0; i < 32; ++i) {
        std::string key = "k" + std::to_string(i);
        ASSERT_EQ((*parsed)[key].as_int(), i);
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
    EXPECT_EQ(extra_object.at("name").as_string(), "alice");
    EXPECT_EQ(extra_object.at("n").as_int(), 1);

    extra_object.assign("n", content::Value(std::int64_t(2)));

    auto encoded = json::to_string(*parsed);
    ASSERT_TRUE(encoded.has_value());

    auto reparsed = json::parse<mixed_payload>(*encoded);
    ASSERT_TRUE(reparsed.has_value());
    EXPECT_EQ(reparsed->id, 7);
    ASSERT_TRUE(reparsed->extra.is_object());
    EXPECT_EQ(reparsed->extra["name"].as_string(), "alice");
    EXPECT_EQ(reparsed->extra["n"].as_int(), 2);
}

TEST_CASE(deep_nested_array_via_json_roundtrip) {
    constexpr int depth = 16;
    std::string text(depth, '[');
    text.push_back('1');
    text.append(depth, ']');

    auto parsed = json::parse<json::Value>(text);
    ASSERT_TRUE(parsed.has_value());

    content::Cursor cursor = parsed->cursor();
    for(int i = 0; i < depth; ++i) {
        ASSERT_TRUE(cursor.is_array());
        ASSERT_EQ(cursor.as_array().size(), 1);
        cursor = cursor[0];
    }
    ASSERT_TRUE(cursor.is_int());
    EXPECT_EQ(cursor.as_int(), 1);

    auto encoded = json::to_string(*parsed);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, text);
}

TEST_CASE(cursor_on_scalar_reports_type_mismatch) {
    content::Value int_val(std::int64_t(42));
    content::Value str_val("hello");
    content::Value bool_val(true);
    content::Value null_val(nullptr);

    auto r1 = int_val["key"];
    EXPECT_FALSE(r1.valid());
    EXPECT_EQ(r1.error(), "expected object, got signed_int");

    auto r2 = str_val[0];
    EXPECT_FALSE(r2.valid());
    EXPECT_EQ(r2.error(), "expected array, got string");

    auto r3 = bool_val["x"];
    EXPECT_FALSE(r3.valid());
    EXPECT_EQ(r3.error(), "expected object, got boolean");

    auto r4 = null_val[0];
    EXPECT_FALSE(r4.valid());
    EXPECT_EQ(r4.error(), "expected array, got null");
}

TEST_CASE(default_cursor_chaining_builds_path) {
    content::Cursor c;
    EXPECT_FALSE(c.valid());
    EXPECT_FALSE(c.has_error());

    auto c1 = c["foo"];
    EXPECT_FALSE(c1.valid());
    EXPECT_EQ(c1.error(), R"(["foo"])");

    auto c2 = c1["bar"];
    EXPECT_FALSE(c2.valid());
    EXPECT_EQ(c2.error(), R"(["foo"] -> ["bar"])");

    auto c3 = c[0];
    EXPECT_FALSE(c3.valid());
    EXPECT_EQ(c3.error(), "[0]");

    auto c4 = c3["x"][1];
    EXPECT_FALSE(c4.valid());
    EXPECT_EQ(c4.error(), R"([0] -> ["x"] -> [1])");
}

TEST_CASE(cursor_explicit_bool_conversion) {
    content::Value val(std::int64_t(1));
    content::Cursor valid_c(val);
    content::Cursor invalid_c;

    EXPECT_TRUE(static_cast<bool>(valid_c));
    EXPECT_FALSE(static_cast<bool>(invalid_c));
}

TEST_CASE(cursor_get_accessors_on_invalid_return_nullopt) {
    content::Cursor c;
    EXPECT_FALSE(c.get_bool().has_value());
    EXPECT_FALSE(c.get_int().has_value());
    EXPECT_FALSE(c.get_uint().has_value());
    EXPECT_FALSE(c.get_double().has_value());
    EXPECT_FALSE(c.get_string().has_value());
    EXPECT_EQ(c.get_array(), nullptr);
    EXPECT_EQ(c.get_object(), nullptr);
}

TEST_CASE(empty_object_find_and_cursor_access) {
    content::Object obj;
    EXPECT_TRUE(obj.empty());
    EXPECT_EQ(obj.size(), 0);
    EXPECT_EQ(obj.find("anything"), nullptr);
    EXPECT_FALSE(obj.contains("anything"));

    content::Value val(std::move(obj));
    auto c = val["key"];
    EXPECT_FALSE(c.valid());
    EXPECT_EQ(c.error(), R"(missing key "key")");
}

TEST_CASE(empty_array_cursor_out_of_range) {
    content::Array arr;
    EXPECT_TRUE(arr.empty());

    content::Value val(std::move(arr));
    auto c = val[0];
    EXPECT_FALSE(c.valid());
    EXPECT_EQ(c.error(), "index 0 out of range (size 0)");
}

TEST_CASE(deep_nested_copy_is_independent) {
    content::Array inner_arr;
    inner_arr.push_back(content::Value(std::int64_t(1)));
    inner_arr.push_back(content::Value(std::int64_t(2)));

    content::Object inner_obj;
    inner_obj.insert("nums", content::Value(std::move(inner_arr)));

    content::Array outer_arr;
    outer_arr.push_back(content::Value(std::move(inner_obj)));

    content::Object root_obj;
    root_obj.insert("data", content::Value(std::move(outer_arr)));

    content::Value original(std::move(root_obj));
    content::Value copy = original;

    // Mutate original deeply
    auto& orig_data = original.as_object().at("data").as_array()[0].as_object();
    orig_data.assign("nums", content::Value("replaced"));

    // Copy should be unaffected
    auto& copy_data = copy.as_object().at("data").as_array()[0].as_object();
    ASSERT_TRUE(copy_data.at("nums").is_array());
    EXPECT_EQ(copy_data.at("nums").as_array().size(), 2);
    EXPECT_EQ(copy_data.at("nums").as_array()[0].as_int(), 1);
    EXPECT_EQ(copy_data.at("nums").as_array()[1].as_int(), 2);

    // Original should reflect mutation
    EXPECT_TRUE(original.as_object().at("data").as_array()[0].as_object().at("nums").is_string());
}

TEST_CASE(array_range_for_iteration) {
    content::Array arr;
    arr.push_back(content::Value(std::int64_t(10)));
    arr.push_back(content::Value(std::int64_t(20)));
    arr.push_back(content::Value(std::int64_t(30)));

    std::int64_t sum = 0;
    for(auto& val: arr) {
        sum += val.as_int();
    }
    EXPECT_EQ(sum, 60);
}

TEST_CASE(array_const_range_for_iteration) {
    content::Array arr;
    arr.push_back(content::Value("a"));
    arr.push_back(content::Value("b"));
    arr.push_back(content::Value("c"));

    const auto& const_arr = arr;
    std::string result;
    for(const auto& val: const_arr) {
        result += val.as_string();
    }
    EXPECT_EQ(result, "abc");
}

TEST_CASE(object_range_for_iteration) {
    content::Object obj;
    obj.insert("x", content::Value(std::int64_t(1)));
    obj.insert("y", content::Value(std::int64_t(2)));
    obj.insert("z", content::Value(std::int64_t(3)));

    std::int64_t sum = 0;
    std::string keys;
    for(auto& [key, value]: obj) {
        keys += key;
        sum += value.as_int();
    }
    EXPECT_EQ(sum, 6);
    EXPECT_EQ(keys, "xyz");
}

TEST_CASE(object_const_range_for_iteration) {
    content::Object obj;
    obj.insert("a", content::Value("hello"));
    obj.insert("b", content::Value("world"));

    const auto& const_obj = obj;
    std::string result;
    for(const auto& [key, value]: const_obj) {
        result += key;
        result += "=";
        result += value.as_string();
        result += ";";
    }
    EXPECT_EQ(result, "a=hello;b=world;");
}

TEST_CASE(array_mutation_during_iteration) {
    content::Array arr;
    arr.push_back(content::Value(std::int64_t(1)));
    arr.push_back(content::Value(std::int64_t(2)));
    arr.push_back(content::Value(std::int64_t(3)));

    for(auto& val: arr) {
        val = content::Value(val.as_int() * 10);
    }

    EXPECT_EQ(arr[0].as_int(), 10);
    EXPECT_EQ(arr[1].as_int(), 20);
    EXPECT_EQ(arr[2].as_int(), 30);
}

TEST_CASE(object_mutation_during_iteration) {
    content::Object obj;
    obj.insert("a", content::Value(std::int64_t(1)));
    obj.insert("b", content::Value(std::int64_t(2)));

    for(auto& [key, value]: obj) {
        value = content::Value(value.as_int() + 100);
    }

    EXPECT_EQ(obj.at("a").as_int(), 101);
    EXPECT_EQ(obj.at("b").as_int(), 102);
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
