#if __has_include(<toml++/toml.hpp>)

#include <string>
#include <vector>

#include "eventide/zest/zest.h"
#include "eventide/serde/serde/annotation.h"
#include "eventide/serde/serde/attrs.h"
#include "eventide/serde/toml.h"

namespace eventide::serde {

namespace {

using toml::from_toml;
using toml::parse;

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

TEST_SUITE(serde_toml_error_message) {

TEST_CASE(missing_required_field) {
    auto result = parse<person>(R"(
age = 25
[addr]
city = "NY"
zip = 10001
)");
    ASSERT_FALSE(result.has_value());
    auto& e = result.error();
    EXPECT_EQ(e.message(), "missing required field 'name'");
    EXPECT_EQ(e.kind, toml::error_kind::type_mismatch);
}

TEST_CASE(unknown_field_denied) {
    auto result = parse<strict_payload>(R"(
id = 1
name = "ok"
extra = true
)");
    ASSERT_FALSE(result.has_value());
    auto& e = result.error();
    EXPECT_EQ(e.message(), "unknown field 'extra'");
    EXPECT_EQ(e.kind, toml::error_kind::type_mismatch);
}

TEST_CASE(nested_field_error_path) {
    auto result = parse<person>(R"(
name = "alice"
age = 30
[addr]
city = "NY"
zip = "wrong"
)");
    ASSERT_FALSE(result.has_value());
    auto& e = result.error();
    EXPECT_EQ(e.format_path(), "addr.zip");
    EXPECT_EQ(e.message(), "type mismatch");
    EXPECT_EQ(e.kind, toml::error_kind::type_mismatch);
}

TEST_CASE(sequence_element_error_path) {
    // TOML does not allow mixed-type arrays, so we use a struct with a vector
    // field and provide a table array where an element has the wrong type.
    // Instead, we use a TOML array with string elements for an int vector field.
    auto result = parse<with_scores>(R"(
name = "bob"
scores = ["bad"]
)");
    ASSERT_FALSE(result.has_value());
    auto& e = result.error();
    EXPECT_EQ(e.format_path(), "scores[0]");
    EXPECT_EQ(e.kind, toml::error_kind::type_mismatch);
}

TEST_CASE(enum_string_error_message) {
    enum_string<color> parsed = color::red;
    auto table = toml::parse_table(R"(__value = "purple")");
    ASSERT_TRUE(table.has_value());
    auto status = from_toml(*table, parsed);
    ASSERT_FALSE(status.has_value());
    auto& e = status.error();
    EXPECT_EQ(e.message(), "unknown enum string value 'purple'");
    EXPECT_EQ(e.kind, toml::error_kind::type_mismatch);
}

TEST_CASE(error_has_location) {
    auto result = parse<person>(R"(
name = "alice"
age = "not_a_number"
)");
    ASSERT_FALSE(result.has_value());
    auto& e = result.error();
    EXPECT_TRUE(e.location().has_value());
    // "age" is on line 3 (line 1 is empty after the raw string opening)
    EXPECT_EQ(e.location()->line, 3);
    EXPECT_EQ(e.location()->column, 7);
}

TEST_CASE(to_string_combines_all) {
    auto result = parse<person>(R"(
name = "alice"
age = 30
[addr]
city = "NY"
zip = "wrong"
)");
    ASSERT_FALSE(result.has_value());
    auto& e = result.error();
    // to_string format: "message at path (line L, column C)"
    EXPECT_EQ(e.to_string(), "type mismatch at addr.zip (line 6, column 7)");
}

};  // TEST_SUITE(serde_toml_error_message)

}  // namespace

}  // namespace eventide::serde

#endif
