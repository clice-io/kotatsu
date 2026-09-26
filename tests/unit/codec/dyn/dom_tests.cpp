#include <cstdint>
#include <string>

#include "kota/zest/zest.h"
#include "kota/codec/dyn/dyn.h"
#include "kota/codec/json/json.h"

namespace kota::codec {

namespace {

struct mixed_payload {
    int id = 0;
    dyn::Value extra;
};

struct dom_payload {
    int id = 0;
    std::string name;
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

ZEST_SUITE(serde_content_dom) {

ZEST_CASE(construct_scalars) {
    dyn::Value null_value{};
    EXPECT(null_value.is_null());

    dyn::Value bool_value(true);
    EXPECT(bool_value.is_bool());
    EXPECT(bool_value.as_bool() == true);

    dyn::Value int_value(std::int64_t(-7));
    EXPECT(int_value.is_int());
    EXPECT(int_value.as_int() == -7);

    dyn::Value uint_value(std::uint64_t(42));
    EXPECT(uint_value.is_int());
    EXPECT(uint_value.as_uint() == std::uint64_t(42));

    dyn::Value double_value(3.5);
    EXPECT(double_value.is_number());
    EXPECT(double_value.as_double() == 3.5);

    dyn::Value string_value("hello");
    EXPECT(string_value.is_string());
    EXPECT(string_value.as_string() == "hello");
}

ZEST_CASE(int_uint_cross_sign_access) {
    dyn::Value big_uint(std::uint64_t{9223372036854775808ULL});
    EXPECT(!big_uint.get_int());
    ASSERT(big_uint.get_uint());
    EXPECT(*big_uint.get_uint() == std::uint64_t{9223372036854775808ULL});

    dyn::Value neg_int(std::int64_t{-1});
    EXPECT(!neg_int.get_uint());
    ASSERT(neg_int.get_int());
    EXPECT(*neg_int.get_int() == std::int64_t{-1});
}

ZEST_CASE(parse_and_view_basic_via_json) {
    auto parsed = json::from_string<dyn::Value>(R"({"a":1,"b":"x","arr":[1,2]})");
    ASSERT(parsed);

    ASSERT(parsed->is_object());
    ASSERT((*parsed)["a"].as_int() == 1);
    ASSERT((*parsed)["b"].as_string() == "x");
    ASSERT((*parsed)["arr"][1].as_int() == 2);
    EXPECT(!(*parsed)["missing"].valid());
}

ZEST_CASE(cursor_miss_describes_failure) {
    auto parsed = json::from_string<dyn::Value>(R"({"a":{"b":[10,20]}})");
    ASSERT(parsed);

    auto missing_key = (*parsed)["zzz"];
    EXPECT(!missing_key.valid());
    EXPECT(missing_key.has_error());
    EXPECT(missing_key.error() == R"(missing key "zzz")");

    auto out_of_range = (*parsed)["a"]["b"][5];
    EXPECT(!out_of_range.valid());
    EXPECT(out_of_range.error() == "index 5 out of range (size 2)");

    auto wrong_kind = (*parsed)["a"]["b"]["x"];
    EXPECT(!wrong_kind.valid());
    EXPECT(wrong_kind.error() == "expected object, got array");
}

ZEST_CASE(cursor_chain_appends_path) {
    auto parsed = json::from_string<dyn::Value>(R"({"a":1})");
    ASSERT(parsed);

    auto deep = (*parsed)["missing"]["x"][3]["y"];
    ASSERT(!deep.valid());
    EXPECT(deep.error() == R"(missing key "missing" -> ["x"] -> [3] -> ["y"])");
}

ZEST_CASE(object_lookup_builds_lazy_index) {
    auto json_text = make_large_object_json(32);
    auto parsed = json::from_string<dyn::Value>(json_text);
    ASSERT(parsed);

    ASSERT(parsed->is_object());
    for(int i = 0; i < 32; ++i) {
        std::string key = "k" + std::to_string(i);
        ASSERT((*parsed)[key].as_int() == i);
    }
}

ZEST_CASE(value_copy_is_deep) {
    dyn::Object obj;
    obj.insert("n", dyn::Value(std::int64_t(1)));

    dyn::Value original(std::move(obj));
    dyn::Value copy = original;

    original.as_object().assign("n", dyn::Value(std::int64_t(2)));

    EXPECT(original.as_object().at("n").as_int() == 2);
    EXPECT(copy.as_object().at("n").as_int() == 1);
}

ZEST_CASE(object_equality_is_order_insensitive) {
    dyn::Object a;
    a.insert("x", dyn::Value(std::int64_t(1)));
    a.insert("y", dyn::Value(std::int64_t(2)));

    dyn::Object b;
    b.insert("y", dyn::Value(std::int64_t(2)));
    b.insert("x", dyn::Value(std::int64_t(1)));

    EXPECT(a == b);
}

ZEST_CASE(mixed_struct_roundtrip_with_dynamic_dom) {
    auto parsed = json::from_string<mixed_payload>(R"({"id":7,"extra":{"name":"alice","n":1}})");
    ASSERT(parsed);
    ASSERT(parsed->id == 7);

    auto& extra_object = parsed->extra.as_object();
    EXPECT(extra_object.at("name").as_string() == "alice");
    EXPECT(extra_object.at("n").as_int() == 1);

    extra_object.assign("n", dyn::Value(std::int64_t(2)));

    auto encoded = json::to_string(*parsed);
    ASSERT(encoded);

    auto reparsed = json::from_string<mixed_payload>(*encoded);
    ASSERT(reparsed);
    EXPECT(reparsed->id == 7);
    ASSERT(reparsed->extra.is_object());
    EXPECT(reparsed->extra["name"].as_string() == "alice");
    EXPECT(reparsed->extra["n"].as_int() == 2);
}

ZEST_CASE(deep_nested_array_via_json_roundtrip) {
    constexpr int depth = 16;
    std::string text(depth, '[');
    text.push_back('1');
    text.append(depth, ']');

    auto parsed = json::from_string<dyn::Value>(text);
    ASSERT(parsed);

    dyn::Cursor cursor = parsed->cursor();
    for(int i = 0; i < depth; ++i) {
        ASSERT(cursor.is_array());
        ASSERT(cursor.as_array().size() == 1);
        cursor = cursor[0];
    }
    ASSERT(cursor.is_int());
    EXPECT(cursor.as_int() == 1);

    auto encoded = json::to_string(*parsed);
    ASSERT(encoded);
    EXPECT(*encoded == text);
}

ZEST_CASE(cursor_on_scalar_reports_type_mismatch) {
    dyn::Value int_val(std::int64_t(42));
    dyn::Value str_val("hello");
    dyn::Value bool_val(true);
    dyn::Value null_val(nullptr);

    auto r1 = int_val["key"];
    EXPECT(!r1.valid());
    EXPECT(r1.error() == "expected object, got signed_int");

    auto r2 = str_val[0];
    EXPECT(!r2.valid());
    EXPECT(r2.error() == "expected array, got string");

    auto r3 = bool_val["x"];
    EXPECT(!r3.valid());
    EXPECT(r3.error() == "expected object, got boolean");

    auto r4 = null_val[0];
    EXPECT(!r4.valid());
    EXPECT(r4.error() == "expected array, got null");
}

ZEST_CASE(default_cursor_chaining_builds_path) {
    dyn::Cursor c;
    EXPECT(!c.valid());
    EXPECT(!c.has_error());

    auto c1 = c["foo"];
    EXPECT(!c1.valid());
    EXPECT(c1.error() == R"(["foo"])");

    auto c2 = c1["bar"];
    EXPECT(!c2.valid());
    EXPECT(c2.error() == R"(["foo"] -> ["bar"])");

    auto c3 = c[0];
    EXPECT(!c3.valid());
    EXPECT(c3.error() == "[0]");

    auto c4 = c3["x"][1];
    EXPECT(!c4.valid());
    EXPECT(c4.error() == R"([0] -> ["x"] -> [1])");
}

ZEST_CASE(cursor_explicit_bool_conversion) {
    dyn::Value val(std::int64_t(1));
    dyn::Cursor valid_c(val);
    dyn::Cursor invalid_c;

    EXPECT(static_cast<bool>(valid_c));
    EXPECT(!static_cast<bool>(invalid_c));
}

ZEST_CASE(cursor_get_accessors_on_invalid_return_nullopt) {
    dyn::Cursor c;
    EXPECT(!c.get_bool());
    EXPECT(!c.get_int());
    EXPECT(!c.get_uint());
    EXPECT(!c.get_double());
    EXPECT(!c.get_string());
    EXPECT(c.get_array() == nullptr);
    EXPECT(c.get_object() == nullptr);
}

ZEST_CASE(empty_object_find_and_cursor_access) {
    dyn::Object obj;
    EXPECT(obj.empty());
    EXPECT(obj.size() == 0);
    EXPECT(obj.find("anything") == nullptr);
    EXPECT(!obj.contains("anything"));

    dyn::Value val(std::move(obj));
    auto c = val["key"];
    EXPECT(!c.valid());
    EXPECT(c.error() == R"(missing key "key")");
}

ZEST_CASE(empty_array_cursor_out_of_range) {
    dyn::Array arr;
    EXPECT(arr.empty());

    dyn::Value val(std::move(arr));
    auto c = val[0];
    EXPECT(!c.valid());
    EXPECT(c.error() == "index 0 out of range (size 0)");
}

ZEST_CASE(deep_nested_copy_is_independent) {
    dyn::Array inner_arr;
    inner_arr.push_back(dyn::Value(std::int64_t(1)));
    inner_arr.push_back(dyn::Value(std::int64_t(2)));

    dyn::Object inner_obj;
    inner_obj.insert("nums", dyn::Value(std::move(inner_arr)));

    dyn::Array outer_arr;
    outer_arr.push_back(dyn::Value(std::move(inner_obj)));

    dyn::Object root_obj;
    root_obj.insert("data", dyn::Value(std::move(outer_arr)));

    dyn::Value original(std::move(root_obj));
    dyn::Value copy = original;

    // Mutate original deeply
    auto& orig_data = original.as_object().at("data").as_array()[0].as_object();
    orig_data.assign("nums", dyn::Value("replaced"));

    // Copy should be unaffected
    auto& copy_data = copy.as_object().at("data").as_array()[0].as_object();
    ASSERT(copy_data.at("nums").is_array());
    EXPECT(copy_data.at("nums").as_array().size() == 2);
    EXPECT(copy_data.at("nums").as_array()[0].as_int() == 1);
    EXPECT(copy_data.at("nums").as_array()[1].as_int() == 2);

    // Original should reflect mutation
    EXPECT(original.as_object().at("data").as_array()[0].as_object().at("nums").is_string());
}

ZEST_CASE(array_range_for_iteration) {
    dyn::Array arr;
    arr.push_back(dyn::Value(std::int64_t(10)));
    arr.push_back(dyn::Value(std::int64_t(20)));
    arr.push_back(dyn::Value(std::int64_t(30)));

    std::int64_t sum = 0;
    for(auto& val: arr) {
        sum += val.as_int();
    }
    EXPECT(sum == 60);
}

ZEST_CASE(array_const_range_for_iteration) {
    dyn::Array arr;
    arr.push_back(dyn::Value("a"));
    arr.push_back(dyn::Value("b"));
    arr.push_back(dyn::Value("c"));

    const auto& const_arr = arr;
    std::string result;
    for(const auto& val: const_arr) {
        result += val.as_string();
    }
    EXPECT(result == "abc");
}

ZEST_CASE(object_range_for_iteration) {
    dyn::Object obj;
    obj.insert("x", dyn::Value(std::int64_t(1)));
    obj.insert("y", dyn::Value(std::int64_t(2)));
    obj.insert("z", dyn::Value(std::int64_t(3)));

    std::int64_t sum = 0;
    std::string keys;
    for(auto& [key, value]: obj) {
        keys += key;
        sum += value.as_int();
    }
    EXPECT(sum == 6);
    EXPECT(keys == "xyz");
}

ZEST_CASE(object_const_range_for_iteration) {
    dyn::Object obj;
    obj.insert("a", dyn::Value("hello"));
    obj.insert("b", dyn::Value("world"));

    const auto& const_obj = obj;
    std::string result;
    for(const auto& [key, value]: const_obj) {
        result += key;
        result += "=";
        result += value.as_string();
        result += ";";
    }
    EXPECT(result == "a=hello;b=world;");
}

ZEST_CASE(array_mutation_during_iteration) {
    dyn::Array arr;
    arr.push_back(dyn::Value(std::int64_t(1)));
    arr.push_back(dyn::Value(std::int64_t(2)));
    arr.push_back(dyn::Value(std::int64_t(3)));

    for(auto& val: arr) {
        val = dyn::Value(val.as_int() * 10);
    }

    EXPECT(arr[0].as_int() == 10);
    EXPECT(arr[1].as_int() == 20);
    EXPECT(arr[2].as_int() == 30);
}

ZEST_CASE(object_mutation_during_iteration) {
    dyn::Object obj;
    obj.insert("a", dyn::Value(std::int64_t(1)));
    obj.insert("b", dyn::Value(std::int64_t(2)));

    for(auto& [key, value]: obj) {
        value = dyn::Value(value.as_int() + 100);
    }

    EXPECT(obj.at("a").as_int() == 101);
    EXPECT(obj.at("b").as_int() == 102);
}

ZEST_CASE(content_deserializer_keeps_temporary_root_value_alive) {
    auto make_dom = []() -> dyn::Value {
        auto parsed = json::from_string<dyn::Value>(R"({"id":7,"name":"alice"})");
        return parsed ? std::move(*parsed) : dyn::Value{};
    };

    dom_payload payload{};
    auto status = dyn::from_dyn(make_dom(), payload);
    ASSERT(status);
    EXPECT(payload == (dom_payload{.id = 7, .name = "alice"}));
}

};  // ZEST_SUITE(serde_content_dom)

}  // namespace

}  // namespace kota::codec
