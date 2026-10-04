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
    ZEXPECT(dyn::Value().kind() == dyn::ValueKind::null_value);
    ZEXPECT(dyn::Value(nullptr).kind() == dyn::ValueKind::null_value);
    ZEXPECT(dyn::Value(true).kind() == dyn::ValueKind::boolean);
    ZEXPECT(dyn::Value(std::int64_t{-7}).kind() == dyn::ValueKind::signed_int);
    ZEXPECT(dyn::Value(std::uint64_t{42}).kind() == dyn::ValueKind::unsigned_int);
    ZEXPECT(dyn::Value(3.5).kind() == dyn::ValueKind::floating);
    ZEXPECT(dyn::Value("hello").kind() == dyn::ValueKind::string);
    ZEXPECT(dyn::Value(std::string_view("hello")).kind() == dyn::ValueKind::string);

    // Narrower integers widen to the 64-bit kind of their signedness.
    ZEXPECT(dyn::Value(std::int8_t{-7}) == dyn::Value(std::int64_t{-7}));
    ZEXPECT(dyn::Value(std::uint16_t{7}) == dyn::Value(std::uint64_t{7}));

    // is_int takes either signedness, is_number floats too.
    dyn::Value count(std::uint64_t{1});
    dyn::Value number(3.5);
    ZEXPECT(count.is_int());
    ZEXPECT(count.is_number());
    ZEXPECT(!number.is_int());
    ZEXPECT(number.is_number());
    ZEXPECT(number.get_double() == 3.5);
    dyn::Value text("hello");
    ZEXPECT(text.is_string());
    ZEXPECT(text.get_string() == "hello");
}

ZEST_CASE(as_accessors_read_their_own_kind) {
    // The get_ accessors convert between the number kinds; the as_ ones read
    // the kind the value holds.
    ZEXPECT(dyn::Value(true).as_bool());
    ZEXPECT(dyn::Value(std::int64_t{-7}).as_int() == -7);
    ZEXPECT(dyn::Value(std::uint64_t{18446744073709551615ULL}).as_uint() ==
            18446744073709551615ULL);
    ZEXPECT(dyn::Value(3.5).as_double() == 3.5);
    ZEXPECT(dyn::Value("hello").as_string() == "hello");
}

ZEST_CASE(integer_accessors_cross_signedness_in_range) {
    dyn::Value big(std::uint64_t{9223372036854775808ULL});
    ZEXPECT(big.is_int());
    ZEXPECT(!big.get_int());
    ZEXPECT(big.get_uint() == std::uint64_t{9223372036854775808ULL});

    dyn::Value negative(std::int64_t{-1});
    ZEXPECT(!negative.get_uint());
    ZEXPECT(negative.get_int() == std::int64_t{-1});

    dyn::Value small(std::uint64_t{7});
    ZEXPECT(small.get_int() == std::int64_t{7});
    ZEXPECT(small.get_double() == 7.0);
}

ZEST_CASE(cursor_reads_nested_values) {
    auto root = nested();
    ZEXPECT(root["a"]["b"][1].get_int() == std::int64_t{20});
    ZEXPECT(root.cursor()["s"].get_string() == "x");
    ZEXPECT(!root["missing"].valid());
}

ZEST_CASE(cursor_miss_fails) {
    auto root = nested();

    auto missing_key = root["zzz"];
    ZEXPECT(!missing_key.valid());
    ZEXPECT(missing_key.has_error());
    ZEXPECT(missing_key.error() == R"(missing key "zzz")");

    auto out_of_range = root["a"]["b"][5];
    ZEXPECT(!out_of_range.valid());
    ZEXPECT(out_of_range.error() == "index 5 out of range (size 2)");

    auto wrong_kind = root["a"]["b"]["x"];
    ZEXPECT(!wrong_kind.valid());
    ZEXPECT(wrong_kind.error() == "expected object, got array");
}

ZEST_CASE(cursor_chain_after_miss_fails) {
    auto root = nested();
    auto deep = root["missing"]["x"][3]["y"];
    ZEXPECT(!deep.valid());
    ZEXPECT(deep.error() == R"(missing key "missing" -> ["x"] -> [3] -> ["y"])");
}

ZEST_CASE(cursor_into_scalar_fails) {
    dyn::Value number(std::int64_t{42});
    auto on_int = number["key"];
    ZEXPECT(!on_int.valid());
    ZEXPECT(on_int.error() == "expected object, got signed_int");

    dyn::Value text("hello");
    auto on_string = text[0];
    ZEXPECT(!on_string.valid());
    ZEXPECT(on_string.error() == "expected array, got string");

    dyn::Value flag(true);
    auto on_bool = flag["x"];
    ZEXPECT(!on_bool.valid());
    ZEXPECT(on_bool.error() == "expected object, got boolean");

    dyn::Value null(nullptr);
    auto on_null = null[0];
    ZEXPECT(!on_null.valid());
    ZEXPECT(on_null.error() == "expected array, got null");
}

ZEST_CASE(default_cursor_chain_fails) {
    dyn::Cursor cursor;
    ZEXPECT(!cursor.valid());
    ZEXPECT(!cursor.has_error());

    auto by_key = cursor["foo"];
    ZEXPECT(!by_key.valid());
    ZEXPECT(by_key.error() == R"(["foo"])");
    ZEXPECT(by_key["bar"].error() == R"(["foo"] -> ["bar"])");

    auto by_index = cursor[0];
    ZEXPECT(by_index.error() == "[0]");
    ZEXPECT(by_index["x"][1].error() == R"([0] -> ["x"] -> [1])");
}

ZEST_CASE(cursor_converts_to_bool) {
    dyn::Value value(std::int64_t{1});
    ZEXPECT(static_cast<bool>(dyn::Cursor(value)));
    ZEXPECT(!static_cast<bool>(dyn::Cursor()));
}

ZEST_CASE(invalid_cursor_accessors_return_nothing) {
    dyn::Cursor cursor;
    ZEXPECT(!cursor.kind());
    ZEXPECT(!cursor.get_bool());
    ZEXPECT(!cursor.get_int());
    ZEXPECT(!cursor.get_uint());
    ZEXPECT(!cursor.get_double());
    ZEXPECT(!cursor.get_string());
    ZEXPECT(cursor.get_array() == nullptr);
    ZEXPECT(cursor.get_object() == nullptr);
}

ZEST_CASE(empty_object_lookup_fails) {
    dyn::Object object;
    ZEXPECT(object.empty());
    ZEXPECT(object.size() == 0U);
    ZEXPECT(object.find("anything") == nullptr);
    ZEXPECT(!object.contains("anything"));

    dyn::Value value(object);
    auto cursor = value["key"];
    ZEXPECT(!cursor.valid());
    ZEXPECT(cursor.error() == R"(missing key "key")");
}

ZEST_CASE(empty_array_index_fails) {
    dyn::Array array;
    ZEXPECT(array.empty());

    dyn::Value value(array);
    auto cursor = value[0];
    ZEXPECT(!cursor.valid());
    ZEXPECT(cursor.error() == "index 0 out of range (size 0)");
}

ZEST_CASE(copy_is_deep) {
    dyn::Value original{
        {"data", dyn::Array{dyn::Object{{"nums", dyn::Array{std::int64_t{1}, std::int64_t{2}}}}}},
    };
    dyn::Value copy = original;

    original.as_object().at("data").as_array()[0].as_object().assign("nums", "replaced");

    ZEXPECT(original["data"][0]["nums"].is_string());
    ZEXPECT(copy ==
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
    ZEXPECT(joined == "abc");
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
    ZEXPECT(joined == "b=world;a=hello;");
}

ZEST_CASE(object_equality_ignores_order) {
    dyn::Object forward;
    forward.insert("x", std::int64_t{1});
    forward.insert("y", std::int64_t{2});

    dyn::Object backward;
    backward.insert("y", std::int64_t{2});
    backward.insert("x", std::int64_t{1});

    ZEXPECT(forward == backward);
}

ZEST_CASE(object_equality_counts_duplicates) {
    dyn::Object twice;
    twice.insert("k", std::int64_t{1});
    twice.insert("k", std::int64_t{2});

    dyn::Object reversed;
    reversed.insert("k", std::int64_t{2});
    reversed.insert("k", std::int64_t{1});
    ZEXPECT(twice == reversed);

    dyn::Object same_value;
    same_value.insert("k", std::int64_t{1});
    same_value.insert("k", std::int64_t{1});
    ZEXPECT(twice != same_value);

    dyn::Object once;
    once.insert("k", std::int64_t{1});
    ZEXPECT(twice != once);
}

ZEST_CASE(kind_decides_equality) {
    // A tree compares kinds as well as values: 1, 1U and 1.0 differ.
    ZEXPECT(dyn::Value(std::int64_t{1}) != dyn::Value(std::uint64_t{1}));
    ZEXPECT(dyn::Value(std::int64_t{1}) != dyn::Value(1.0));
}

};  // ZEST_SUITE(codec_dyn_document)

}  // namespace

}  // namespace kota::codec
