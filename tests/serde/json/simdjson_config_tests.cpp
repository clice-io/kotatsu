#include <string>

#include "eventide/zest/zest.h"
#include "eventide/serde/config.h"
#include "eventide/serde/json/simd_deserializer.h"
#include "eventide/serde/json/simd_serializer.h"
#include "eventide/serde/serde.h"

namespace eventide::serde {

namespace {

using json::simd::from_json;
using json::simd::to_json;

struct nested_payload {
    int some_value = 0;
};

struct protocol_payload {
    int request_id = 0;
    std::string user_name;
    nested_payload nested_info{};
};

struct rename_override_payload {
    rename<std::string, "uid"> user_name;
    int request_id = 0;
};

struct camel_config {
    using field_rename = rename_policy::lower_camel;
};

TEST_SUITE(serde_simdjson_config) {

TEST_CASE(default_field_rename_is_identity) {
    protocol_payload input{
        .request_id = 7,
        .user_name = "alice",
        .nested_info = {.some_value = 3},
    };

    auto encoded = to_json(input);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"request_id":7,"user_name":"alice","nested_info":{"some_value":3}})");

    protocol_payload parsed{};
    auto status =
        from_json(R"({"request_id":7,"user_name":"alice","nested_info":{"some_value":3}})", parsed);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(parsed.request_id, 7);
    EXPECT_EQ(parsed.user_name, "alice");
    EXPECT_EQ(parsed.nested_info.some_value, 3);
}

TEST_CASE(compile_time_lower_camel_field_rename) {
    protocol_payload input{
        .request_id = 8,
        .user_name = "bob",
        .nested_info = {.some_value = 11},
    };

    auto encoded = to_json<camel_config>(input);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"requestId":8,"userName":"bob","nestedInfo":{"someValue":11}})");

    protocol_payload parsed{};
    auto status =
        from_json<camel_config>(R"({"requestId":8,"userName":"bob","nestedInfo":{"someValue":11}})",
                                parsed);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(parsed.request_id, 8);
    EXPECT_EQ(parsed.user_name, "bob");
    EXPECT_EQ(parsed.nested_info.some_value, 11);
}

TEST_CASE(different_configs_in_same_scope) {
    protocol_payload input{
        .request_id = 9,
        .user_name = "carol",
        .nested_info = {.some_value = 21},
    };

    auto camel_encoded = to_json<camel_config>(input);
    ASSERT_TRUE(camel_encoded.has_value());
    EXPECT_EQ(*camel_encoded,
              R"({"requestId":9,"userName":"carol","nestedInfo":{"someValue":21}})");

    auto default_encoded = to_json(input);
    ASSERT_TRUE(default_encoded.has_value());
    EXPECT_EQ(*default_encoded,
              R"({"request_id":9,"user_name":"carol","nested_info":{"some_value":21}})");
}

TEST_CASE(compile_time_config_with_attr_override) {
    rename_override_payload renamed{};
    renamed.user_name = "id-1";
    renamed.request_id = 5;
    auto encoded = to_json<camel_config>(renamed);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"uid":"id-1","requestId":5})");

    rename_override_payload parsed{};
    auto status = from_json<camel_config>(R"({"uid":"id-2","requestId":6})", parsed);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(parsed.user_name, "id-2");
    EXPECT_EQ(parsed.request_id, 6);
}

};  // TEST_SUITE(serde_simdjson_config)

}  // namespace

}  // namespace eventide::serde
