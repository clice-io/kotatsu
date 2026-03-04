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

std::string prefix_policy(bool is_serialize, std::string_view value) {
    if(is_serialize) {
        return std::string("x_") + std::string(value);
    }
    if(value.starts_with("x_")) {
        return std::string(value.substr(2));
    }
    return std::string(value);
}

TEST_SUITE(serde_simdjson_config) {

TEST_CASE(default_field_rename_is_identity) {
    config::reset();

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

TEST_CASE(thread_local_lower_camel_field_rename) {
    config::reset();
    config::set_field_rename_policy<rename_policy::lower_camel>();

    protocol_payload input{
        .request_id = 8,
        .user_name = "bob",
        .nested_info = {.some_value = 11},
    };

    auto encoded = to_json(input);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, R"({"requestId":8,"userName":"bob","nestedInfo":{"someValue":11}})");

    protocol_payload parsed{};
    auto status =
        from_json(R"({"requestId":8,"userName":"bob","nestedInfo":{"someValue":11}})", parsed);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(parsed.request_id, 8);
    EXPECT_EQ(parsed.user_name, "bob");
    EXPECT_EQ(parsed.nested_info.some_value, 11);
}

TEST_CASE(scoped_config_restores_previous_policy) {
    config::reset();

    protocol_payload input{
        .request_id = 9,
        .user_name = "carol",
        .nested_info = {.some_value = 21},
    };

    {
        config::runtime_config cfg{};
        cfg.field_rename = &config::detail::apply_policy_rename<rename_policy::lower_camel>;
        config::scoped_config guard(cfg);

        auto encoded = to_json(input);
        ASSERT_TRUE(encoded.has_value());
        EXPECT_EQ(*encoded, R"({"requestId":9,"userName":"carol","nestedInfo":{"someValue":21}})");
    }

    auto encoded_after_scope = to_json(input);
    ASSERT_TRUE(encoded_after_scope.has_value());
    EXPECT_EQ(*encoded_after_scope,
              R"({"request_id":9,"user_name":"carol","nested_info":{"some_value":21}})");
}

TEST_CASE(custom_field_rename_fn_and_attr_override) {
    config::reset();

    config::runtime_config cfg{};
    cfg.field_rename = &prefix_policy;
    config::scoped_config guard(cfg);

    protocol_payload input{
        .request_id = 10,
        .user_name = "neo",
        .nested_info = {.some_value = 42},
    };
    auto encoded = to_json(input);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded,
              R"({"x_request_id":10,"x_user_name":"neo","x_nested_info":{"x_some_value":42}})");

    protocol_payload parsed{};
    auto status =
        from_json(R"({"x_request_id":10,"x_user_name":"neo","x_nested_info":{"x_some_value":42}})",
                  parsed);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(parsed.request_id, 10);
    EXPECT_EQ(parsed.user_name, "neo");
    EXPECT_EQ(parsed.nested_info.some_value, 42);

    rename_override_payload renamed{};
    renamed.user_name = "id-1";
    renamed.request_id = 5;
    auto encoded_renamed = to_json(renamed);
    ASSERT_TRUE(encoded_renamed.has_value());
    EXPECT_EQ(*encoded_renamed, R"({"uid":"id-1","x_request_id":5})");

    rename_override_payload parsed_renamed{};
    auto renamed_status = from_json(R"({"uid":"id-2","x_request_id":6})", parsed_renamed);
    ASSERT_TRUE(renamed_status.has_value());
    EXPECT_EQ(parsed_renamed.user_name, "id-2");
    EXPECT_EQ(parsed_renamed.request_id, 6);
}

};  // TEST_SUITE(serde_simdjson_config)

}  // namespace

}  // namespace eventide::serde
