#include <cstdint>
#include <string>
#include <string_view>

#include "kota/zest/zest.h"
#include "kota/codec/dyn/document.h"

namespace kota::codec {

namespace {

/// {"a": {"b": [10, 20]}, "s": "x"}
dyn::Value nested() {
    return dyn::Value{
        {"a", dyn::Object{{"b", dyn::Array{std::int64_t{10}, std::int64_t{20}}}}},
        {"s", "x"                                                               },
    };
}

ZEST_SUITE(codec_dyn_document) {

ZEST_CASE(scalars_hold_their_kind) {
    EXPECT(dyn::Value().kind() == dyn::ValueKind::null_value);
    EXPECT(dyn::Value(nullptr).kind() == dyn::ValueKind::null_value);
    EXPECT(dyn::Value(true).kind() == dyn::ValueKind::boolean);
    EXPECT(dyn::Value(std::int64_t{-7}).kind() == dyn::ValueKind::signed_int);
    EXPECT(dyn::Value(std::uint64_t{42}).kind() == dyn::ValueKind::unsigned_int);
    EXPECT(dyn::Value(3.5).kind() == dyn::ValueKind::floating);
    EXPECT(dyn::Value("hello").kind() == dyn::ValueKind::string);
    EXPECT(dyn::Value(std::string_view("hello")).kind() == dyn::ValueKind::string);

    // Narrower integers widen to the 64-bit kind of their signedness.
    EXPECT(dyn::Value(std::int8_t{-7}) == dyn::Value(std::int64_t{-7}));
    EXPECT(dyn::Value(std::uint16_t{7}) == dyn::Value(std::uint64_t{7}));

    // is_int takes either signedness, is_number floats too.
    dyn::Value count(std::uint64_t{1});
    dyn::Value number(3.5);
    EXPECT(count.is_int());
    EXPECT(count.is_number());
    EXPECT(!number.is_int());
    EXPECT(number.is_number());
    EXPECT(number.get_double() == 3.5);
    dyn::Value text("hello");
    EXPECT(text.is_string());
    EXPECT(text.get_string() == "hello");
}

ZEST_CASE(integer_accessors_cross_signedness_in_range) {
    dyn::Value big(std::uint64_t{9223372036854775808ULL});
    EXPECT(big.is_int());
    EXPECT(!big.get_int());
    EXPECT(big.get_uint() == std::uint64_t{9223372036854775808ULL});

    dyn::Value negative(std::int64_t{-1});
    EXPECT(!negative.get_uint());
    EXPECT(negative.get_int() == std::int64_t{-1});

    dyn::Value small(std::uint64_t{7});
    EXPECT(small.get_int() == std::int64_t{7});
    EXPECT(small.get_double() == 7.0);
}

ZEST_CASE(cursor_reads_nested_values) {
    auto root = nested();
    EXPECT(root["a"]["b"][1].get_int() == std::int64_t{20});
    EXPECT(root.cursor()["s"].get_string() == "x");
    EXPECT(!root["missing"].valid());
}

ZEST_CASE(cursor_miss_fails) {
    auto root = nested();

    auto missing_key = root["zzz"];
    EXPECT(!missing_key.valid());
    EXPECT(missing_key.has_error());
    EXPECT(missing_key.error() == R"(missing key "zzz")");

    auto out_of_range = root["a"]["b"][5];
    EXPECT(!out_of_range.valid());
    EXPECT(out_of_range.error() == "index 5 out of range (size 2)");

    auto wrong_kind = root["a"]["b"]["x"];
    EXPECT(!wrong_kind.valid());
    EXPECT(wrong_kind.error() == "expected object, got array");
}

ZEST_CASE(cursor_chain_after_miss_fails) {
    auto root = nested();
    auto deep = root["missing"]["x"][3]["y"];
    EXPECT(!deep.valid());
    EXPECT(deep.error() == R"(missing key "missing" -> ["x"] -> [3] -> ["y"])");
}

ZEST_CASE(cursor_into_scalar_fails) {
    dyn::Value number(std::int64_t{42});
    auto on_int = number["key"];
    EXPECT(!on_int.valid());
    EXPECT(on_int.error() == "expected object, got signed_int");

    dyn::Value text("hello");
    auto on_string = text[0];
    EXPECT(!on_string.valid());
    EXPECT(on_string.error() == "expected array, got string");

    dyn::Value flag(true);
    auto on_bool = flag["x"];
    EXPECT(!on_bool.valid());
    EXPECT(on_bool.error() == "expected object, got boolean");

    dyn::Value null(nullptr);
    auto on_null = null[0];
    EXPECT(!on_null.valid());
    EXPECT(on_null.error() == "expected array, got null");
}

ZEST_CASE(default_cursor_chain_fails) {
    dyn::Cursor cursor;
    EXPECT(!cursor.valid());
    EXPECT(!cursor.has_error());

    auto by_key = cursor["foo"];
    EXPECT(!by_key.valid());
    EXPECT(by_key.error() == R"(["foo"])");
    EXPECT(by_key["bar"].error() == R"(["foo"] -> ["bar"])");

    auto by_index = cursor[0];
    EXPECT(by_index.error() == "[0]");
    EXPECT(by_index["x"][1].error() == R"([0] -> ["x"] -> [1])");
}

ZEST_CASE(cursor_converts_to_bool) {
    dyn::Value value(std::int64_t{1});
    EXPECT(static_cast<bool>(dyn::Cursor(value)));
    EXPECT(!static_cast<bool>(dyn::Cursor()));
}

ZEST_CASE(invalid_cursor_accessors_return_nothing) {
    dyn::Cursor cursor;
    EXPECT(!cursor.kind());
    EXPECT(!cursor.get_bool());
    EXPECT(!cursor.get_int());
    EXPECT(!cursor.get_uint());
    EXPECT(!cursor.get_double());
    EXPECT(!cursor.get_string());
    EXPECT(cursor.get_array() == nullptr);
    EXPECT(cursor.get_object() == nullptr);
}

ZEST_CASE(empty_object_lookup_fails) {
    dyn::Object object;
    EXPECT(object.empty());
    EXPECT(object.size() == 0U);
    EXPECT(object.find("anything") == nullptr);
    EXPECT(!object.contains("anything"));

    dyn::Value value(object);
    auto cursor = value["key"];
    EXPECT(!cursor.valid());
    EXPECT(cursor.error() == R"(missing key "key")");
}

ZEST_CASE(empty_array_index_fails) {
    dyn::Array array;
    EXPECT(array.empty());

    dyn::Value value(array);
    auto cursor = value[0];
    EXPECT(!cursor.valid());
    EXPECT(cursor.error() == "index 0 out of range (size 0)");
}

ZEST_CASE(copy_is_deep) {
    dyn::Value original{
        {"data", dyn::Array{dyn::Object{{"nums", dyn::Array{std::int64_t{1}, std::int64_t{2}}}}}},
    };
    dyn::Value copy = original;

    original.as_object().at("data").as_array()[0].as_object().assign("nums", "replaced");

    EXPECT(original["data"][0]["nums"].is_string());
    EXPECT(copy ==
           (dyn::Value{
               {"data",
                dyn::Array{dyn::Object{{"nums", dyn::Array{std::int64_t{1}, std::int64_t{2}}}}}},
    }));
}

ZEST_CASE(array_iterates_in_order) {
    const dyn::Array array{"a", "b", "c"};
    std::string joined;
    for(const auto& value: array) {
        joined += value.as_string();
    }
    EXPECT(joined == "abc");
}

ZEST_CASE(object_iterates_in_insertion_order) {
    dyn::Object object;
    object.insert("b", "world");
    object.insert("a", "hello");

    const auto& view = object;
    std::string joined;
    for(const auto& [key, value]: view) {
        joined += key;
        joined += '=';
        joined += value.as_string();
        joined += ';';
    }
    EXPECT(joined == "b=world;a=hello;");
}

ZEST_CASE(object_equality_ignores_order) {
    dyn::Object forward;
    forward.insert("x", std::int64_t{1});
    forward.insert("y", std::int64_t{2});

    dyn::Object backward;
    backward.insert("y", std::int64_t{2});
    backward.insert("x", std::int64_t{1});

    EXPECT(forward == backward);
}

ZEST_CASE(object_equality_counts_duplicates) {
    dyn::Object twice;
    twice.insert("k", std::int64_t{1});
    twice.insert("k", std::int64_t{2});

    dyn::Object reversed;
    reversed.insert("k", std::int64_t{2});
    reversed.insert("k", std::int64_t{1});
    EXPECT(twice == reversed);

    dyn::Object same_value;
    same_value.insert("k", std::int64_t{1});
    same_value.insert("k", std::int64_t{1});
    EXPECT(twice != same_value);

    dyn::Object once;
    once.insert("k", std::int64_t{1});
    EXPECT(twice != once);
}

ZEST_CASE(kind_decides_equality) {
    // A tree compares kinds as well as values: 1, 1U and 1.0 differ.
    EXPECT(dyn::Value(std::int64_t{1}) != dyn::Value(std::uint64_t{1}));
    EXPECT(dyn::Value(std::int64_t{1}) != dyn::Value(1.0));
}

};  // ZEST_SUITE(codec_dyn_document)

}  // namespace

}  // namespace kota::codec
