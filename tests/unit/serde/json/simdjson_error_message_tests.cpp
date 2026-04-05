#include <cstdint>
#include <string>
#include <vector>

#include "eventide/zest/zest.h"
#include "eventide/serde/json/deserializer.h"
#include "eventide/serde/serde/annotation.h"
#include "eventide/serde/serde/attrs.h"
#include "eventide/serde/serde/serde.h"

namespace eventide::serde {

namespace {

using json::from_json;

struct address {
    std::string city;
    int zip = 0;
};

struct person {
    std::string name;
    int age = 0;
    address addr;
};

struct strict_struct {
    int id = 0;
    std::string name;
};

using strict_payload = annotation<strict_struct, schema::deny_unknown_fields>;

enum class color { red, green, blue };

struct with_scores {
    std::string name;
    std::vector<int> scores;
};

TEST_SUITE(serde_simdjson_error_message) {

TEST_CASE(missing_required_field) {
    person parsed{};
    auto status = from_json(R"({"age": 25, "addr": {"city": "NY", "zip": 10001}})", parsed);
    EXPECT_FALSE(status.has_value());
    EXPECT_EQ(status.error().kind, json::error_kind::type_mismatch);
    EXPECT_EQ(status.error().message(), "missing required field 'name'");
}

TEST_CASE(unknown_field_denied) {
    strict_payload parsed{};
    auto status = from_json(R"({"id": 1, "name": "ok", "extra": true})", parsed);
    EXPECT_FALSE(status.has_value());
    EXPECT_EQ(status.error().kind, json::error_kind::type_mismatch);
    EXPECT_EQ(status.error().message(), "unknown field 'extra'");
}

TEST_CASE(nested_field_error_path) {
    person parsed{};
    auto status =
        from_json(R"({"name": "alice", "age": 30, "addr": {"city": "NY", "zip": "wrong"}})",
                  parsed);
    EXPECT_FALSE(status.has_value());
    EXPECT_EQ(status.error().message(), "type mismatch");
    EXPECT_EQ(status.error().format_path(), "addr.zip");
}

TEST_CASE(sequence_element_error_path) {
    std::vector<int> parsed;
    auto status = from_json(R"([1, 2, "bad", 4])", parsed);
    EXPECT_FALSE(status.has_value());
    EXPECT_EQ(status.error().message(), "type mismatch");
    EXPECT_EQ(status.error().format_path(), "[2]");
}

TEST_CASE(nested_sequence_error_path) {
    with_scores parsed{};
    auto status = from_json(R"({"name": "bob", "scores": [10, "bad", 30]})", parsed);
    EXPECT_FALSE(status.has_value());
    EXPECT_EQ(status.error().message(), "type mismatch");
    EXPECT_EQ(status.error().format_path(), "scores[1]");
}

TEST_CASE(enum_string_error_message) {
    enum_string<color> parsed = color::red;
    auto status = from_json(R"("yellow")", parsed);
    EXPECT_FALSE(status.has_value());
    EXPECT_EQ(status.error().message(), "unknown enum string value 'yellow'");
}

TEST_CASE(number_out_of_range) {
    std::uint8_t parsed = 0;
    auto status = from_json("300", parsed);
    EXPECT_FALSE(status.has_value());
    EXPECT_EQ(status.error().kind, json::error_kind::number_out_of_range);
    EXPECT_EQ(status.error().message(), "number out of range");
}

TEST_CASE(error_has_location) {
    person parsed{};
    auto status = from_json(R"({
  "name": "alice",
  "age": "not_a_number"
})",
                            parsed);
    EXPECT_FALSE(status.has_value());
    EXPECT_EQ(status.error().message(), "type mismatch");
    EXPECT_EQ(status.error().format_path(), "age");
    EXPECT_TRUE(status.error().location().has_value());
    EXPECT_EQ(status.error().location()->line, 3u);
    EXPECT_EQ(status.error().location()->column, 10u);
    EXPECT_EQ(status.error().location()->byte_offset, 30u);
}

TEST_CASE(to_string_combines_all) {
    person parsed{};
    auto status =
        from_json(R"({"name": "alice", "age": 30, "addr": {"city": "NY", "zip": "wrong"}})",
                  parsed);
    EXPECT_FALSE(status.has_value());
    EXPECT_EQ(status.error().to_string(), "type mismatch at addr.zip (line 1, column 60)");
}

};  // TEST_SUITE(serde_simdjson_error_message)

}  // namespace

}  // namespace eventide::serde
