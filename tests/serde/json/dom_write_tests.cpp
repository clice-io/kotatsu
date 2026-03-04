#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>

#include "eventide/zest/zest.h"
#include "eventide/serde/json/dom.h"

namespace eventide::serde::json {

namespace {

static_assert(dom_writable_value_v<std::nullptr_t>);
static_assert(dom_writable_value_v<bool>);
static_assert(dom_writable_value_v<int>);
static_assert(dom_writable_value_v<unsigned int>);
static_assert(dom_writable_value_v<double>);
static_assert(dom_writable_value_v<std::string_view>);
static_assert(dom_writable_value_v<const char*>);
static_assert(dom_writable_value_v<char[8]>);
static_assert(!dom_writable_value_v<std::string>);
static_assert(!dom_writable_value_v<ValueRef>);
static_assert(!dom_writable_value_v<ArrayRef>);
static_assert(!dom_writable_value_v<ObjectRef>);

auto parse_immutable_value(std::string_view json) -> std::optional<Value> {
    yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
    if(doc == nullptr) {
        return std::nullopt;
    } else {
        return Value::from_immutable_doc(doc);
    }
}

auto parse_mutable_value(std::string_view json) -> std::optional<Value> {
    yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
    if(doc == nullptr) {
        return std::nullopt;
    } else {
        yyjson_mut_doc* mutable_doc = yyjson_doc_mut_copy(doc, nullptr);
        yyjson_doc_free(doc);
        if(mutable_doc == nullptr) {
            return std::nullopt;
        } else {
            return Value::from_mutable_doc(mutable_doc);
        }
    }
}

auto parse_mutable_array(std::string_view json) -> std::optional<Array> {
    auto value = parse_mutable_value(json);
    if(!value.has_value()) {
        return std::nullopt;
    } else {
        return value->get_array();
    }
}

auto parse_mutable_object(std::string_view json) -> std::optional<Object> {
    auto value = parse_mutable_value(json);
    if(!value.has_value()) {
        return std::nullopt;
    } else {
        return value->get_object();
    }
}

auto must_parse_immutable_value(std::string_view json) -> Value {
    auto value = parse_immutable_value(json);
    assert(value.has_value());
    return std::move(*value);
}

auto must_parse_mutable_value(std::string_view json) -> Value {
    auto value = parse_mutable_value(json);
    assert(value.has_value());
    return std::move(*value);
}

auto must_parse_mutable_array(std::string_view json) -> Array {
    auto value = parse_mutable_array(json);
    assert(value.has_value());
    return std::move(*value);
}

auto must_parse_mutable_object(std::string_view json) -> Object {
    auto value = parse_mutable_object(json);
    assert(value.has_value());
    return std::move(*value);
}

TEST_SUITE(serde_json_yyjson_dom2_write) {

TEST_CASE(value_parse_default) {
    auto value = Value::parse(R"({"a":1})");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->as_object()["a"].as_int(), 1);
}

TEST_CASE(value_parse_with_yyjson_flags) {
    Value::parse_options options{};
    options.flags = YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_TRAILING_COMMAS;

    auto value = Value::parse("{/*comment*/\"a\":1,}", options);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->as_object()["a"].as_int(), 1);
}

TEST_CASE(value_set_scalars) {
    auto value = must_parse_mutable_value("0");

    ASSERT_TRUE(value.set(nullptr).has_value());
    EXPECT_TRUE(value.is_null());

    ASSERT_TRUE(value.set(true).has_value());
    EXPECT_EQ(value.as_bool(), true);

    ASSERT_TRUE(value.set(std::int64_t(-7)).has_value());
    EXPECT_EQ(value.as_int(), -7);

    ASSERT_TRUE(value.set(std::uint64_t(42)).has_value());
    EXPECT_EQ(value.as_uint(), std::uint64_t(42));

    ASSERT_TRUE(value.set(3.5).has_value());
    EXPECT_EQ(value.as_double(), 3.5);

    ASSERT_TRUE(value.set(std::string_view("hello")).has_value());
    EXPECT_EQ(value.as_string(), std::string_view("hello"));

    ASSERT_TRUE(value.set("world").has_value());
    EXPECT_EQ(value.as_string(), std::string_view("world"));
}

TEST_CASE(value_operator_assign_changes_kind) {
    auto value = must_parse_mutable_value("1");

    value = "x";
    EXPECT_TRUE(value.is_string());
    EXPECT_EQ(value.as_string(), std::string_view("x"));

    auto source_object = must_parse_immutable_value(R"({"k":1})");
    value = source_object.as_object();
    EXPECT_TRUE(value.is_object());
    EXPECT_EQ(value.as_object()["k"].as_int(), 1);

    auto source_array = must_parse_immutable_value("[1,2]");
    value = source_array.as_array();
    EXPECT_TRUE(value.is_array());
    EXPECT_EQ(value.as_array().size(), std::size_t(2));
    EXPECT_EQ(value.as_array()[1].as_int(), 2);
}

TEST_CASE(array_push_back_with_scalars_and_dom) {
    auto array = must_parse_mutable_array("[]");

    ASSERT_TRUE(array.push_back(nullptr).has_value());
    ASSERT_TRUE(array.push_back(true).has_value());
    ASSERT_TRUE(array.push_back(2).has_value());
    ASSERT_TRUE(array.push_back("x").has_value());

    auto source_object = must_parse_immutable_value(R"({"a":1})");
    ASSERT_TRUE(array.push_back(source_object.as_object()).has_value());

    auto source_array = must_parse_immutable_value("[9]");
    ASSERT_TRUE(array.push_back(source_array.as_array()).has_value());

    EXPECT_EQ(array.size(), std::size_t(6));
    EXPECT_TRUE(array[0].is_null());
    EXPECT_EQ(array[1].as_bool(), true);
    EXPECT_EQ(array[2].as_int(), 2);
    EXPECT_EQ(array[3].as_string(), std::string_view("x"));
    EXPECT_EQ(array[4].as_object()["a"].as_int(), 1);
    EXPECT_EQ(array[5].as_array()[0].as_int(), 9);
}

TEST_CASE(array_insert_bounds_and_positions) {
    auto array = must_parse_mutable_array("[1,3]");

    ASSERT_TRUE(array.insert(1, 2).has_value());
    ASSERT_TRUE(array.insert(3, 4).has_value());

    EXPECT_EQ(array.size(), std::size_t(4));
    EXPECT_EQ(array[0].as_int(), 1);
    EXPECT_EQ(array[1].as_int(), 2);
    EXPECT_EQ(array[2].as_int(), 3);
    EXPECT_EQ(array[3].as_int(), 4);

    auto out_of_range = array.insert(5, 0);
    ASSERT_FALSE(out_of_range.has_value());
    EXPECT_EQ(out_of_range.error(), error_kind::index_out_of_bounds);
}

TEST_CASE(object_insert_rejects_duplicate_key) {
    auto object = must_parse_mutable_object("{}");

    ASSERT_TRUE(object.insert("a", 1).has_value());
    auto duplicate = object.insert("a", 2);
    ASSERT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error(), error_kind::already_exists);
    EXPECT_EQ(object["a"].as_int(), 1);
}

TEST_CASE(object_assign_supports_upsert) {
    auto object = must_parse_mutable_object(R"({"a":1})");

    ASSERT_TRUE(object.assign("a", 2).has_value());
    ASSERT_TRUE(object.assign("b", 3).has_value());

    EXPECT_EQ(object["a"].as_int(), 2);
    EXPECT_EQ(object["b"].as_int(), 3);
}

TEST_CASE(cow_immutable_doc_on_write) {
    auto value = must_parse_immutable_value(R"({"n":1})");
    auto copy = value;
    auto object = value.as_object();

    ASSERT_TRUE(object.assign("n", 2).has_value());

    EXPECT_EQ(object["n"].as_int(), 2);
    EXPECT_EQ(copy.as_object()["n"].as_int(), 1);
    EXPECT_TRUE(object.mutable_doc());
    EXPECT_FALSE(copy.mutable_doc());
}

TEST_CASE(cow_shared_mutable_doc_on_write) {
    auto value = must_parse_mutable_value(R"({"n":1})");
    auto copy = value;
    auto object = value.as_object();

    ASSERT_TRUE(object.assign("n", 2).has_value());

    EXPECT_EQ(object["n"].as_int(), 2);
    EXPECT_EQ(copy.as_object()["n"].as_int(), 1);
    EXPECT_EQ(object.use_count(), 1);
    EXPECT_EQ(copy.use_count(), 2);
}

TEST_CASE(write_api_invalid_state_errors) {
    Value value;
    auto value_status = value.set(1);
    ASSERT_FALSE(value_status.has_value());
    EXPECT_EQ(value_status.error(), error_kind::invalid_state);

    Array array;
    auto array_status = array.push_back(1);
    ASSERT_FALSE(array_status.has_value());
    EXPECT_EQ(array_status.error(), error_kind::invalid_state);

    Object object;
    auto object_insert = object.insert("a", 1);
    ASSERT_FALSE(object_insert.has_value());
    EXPECT_EQ(object_insert.error(), error_kind::invalid_state);

    auto object_assign = object.assign("a", 1);
    ASSERT_FALSE(object_assign.has_value());
    EXPECT_EQ(object_assign.error(), error_kind::invalid_state);
}

};  // TEST_SUITE(serde_json_yyjson_dom2_write)

}  // namespace

}  // namespace eventide::serde::json
