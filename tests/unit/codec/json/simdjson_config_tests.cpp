#include <string>

#include "kota/zest/zest.h"
#include "kota/codec/json/json.h"

namespace kota::codec {

using namespace meta;

namespace {

using json::from_string;
using json::to_string;

struct nested_payload {
    int some_value = 0;
};

struct protocol_payload {
    int request_id = 0;
    std::string user_name;
    nested_payload nested_info{};
};

struct rename_override_payload {
    KOTATSU_ANNOTATE(rename = "uid")
    <std::string> user_name;
    int request_id = 0;
};

struct ambiguous_camel_payload {
    int user_id = 0;
    int userId = 0;
};

struct camel_config {
    using field_rename = rename_policy::lower_camel;
};

ZEST_SUITE(serde_simdjson_config) {

ZEST_CASE(default_identity_rename) {
    protocol_payload input{
        .request_id = 7,
        .user_name = "alice",
        .nested_info = {.some_value = 3},
    };

    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"request_id":7,"user_name":"alice","nested_info":{"some_value":3}})");

    protocol_payload parsed{};
    auto status =
        from_string(R"({"request_id":7,"user_name":"alice","nested_info":{"some_value":3}})",
                    parsed);
    ASSERT(status);
    EXPECT(parsed.request_id == 7);
    EXPECT(parsed.user_name == "alice");
    EXPECT(parsed.nested_info.some_value == 3);
}

ZEST_CASE(lower_camel_rename) {
    protocol_payload input{
        .request_id = 8,
        .user_name = "bob",
        .nested_info = {.some_value = 11},
    };

    auto encoded = to_string<camel_config>(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"requestId":8,"userName":"bob","nestedInfo":{"someValue":11}})");

    protocol_payload parsed{};
    auto status = from_string<camel_config>(
        R"({"requestId":8,"userName":"bob","nestedInfo":{"someValue":11}})",
        parsed);
    ASSERT(status);
    EXPECT(parsed.request_id == 8);
    EXPECT(parsed.user_name == "bob");
    EXPECT(parsed.nested_info.some_value == 11);
}

ZEST_CASE(mixed_configs) {
    protocol_payload input{
        .request_id = 9,
        .user_name = "carol",
        .nested_info = {.some_value = 21},
    };

    auto camel_encoded = to_string<camel_config>(input);
    ASSERT(camel_encoded);
    EXPECT(*camel_encoded == R"({"requestId":9,"userName":"carol","nestedInfo":{"someValue":21}})");

    auto default_encoded = to_string(input);
    ASSERT(default_encoded);
    EXPECT(*default_encoded ==
           R"({"request_id":9,"user_name":"carol","nested_info":{"some_value":21}})");
}

ZEST_CASE(config_with_attr_override) {
    rename_override_payload renamed{};
    renamed.user_name = "id-1";
    renamed.request_id = 5;
    auto encoded = to_string<camel_config>(renamed);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"uid":"id-1","requestId":5})");

    rename_override_payload parsed{};
    auto status = from_string<camel_config>(R"({"uid":"id-2","requestId":6})", parsed);
    ASSERT(status);
    EXPECT(parsed.user_name == "id-2");
    EXPECT(parsed.request_id == 6);
}

ZEST_CASE(rename_collision_fails) {
    ambiguous_camel_payload parsed{};
    auto status = from_string<camel_config>(R"({"userId":1})", parsed);
    EXPECT(!status);
}

ZEST_CASE(to_string_with_config) {
    protocol_payload input{.request_id = 5, .user_name = "eve", .nested_info = {.some_value = 1}};
    auto encoded = json::to_string<camel_config>(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"requestId":5,"userName":"eve","nestedInfo":{"someValue":1}})");
}

ZEST_CASE(parse_with_config) {
    protocol_payload parsed{};
    auto status = json::from_string<camel_config>(
        R"({"requestId":3,"userName":"dan","nestedInfo":{"someValue":7}})",
        parsed);
    ASSERT(status);
    EXPECT(parsed.request_id == 3);
    EXPECT(parsed.user_name == "dan");
    EXPECT(parsed.nested_info.some_value == 7);
}

ZEST_CASE(parse_value_with_config) {
    auto result = json::from_string<protocol_payload, camel_config>(
        R"({"requestId":2,"userName":"fay","nestedInfo":{"someValue":9}})");
    ASSERT(result);
    EXPECT(result->request_id == 2);
    EXPECT(result->user_name == "fay");
    EXPECT(result->nested_info.some_value == 9);
}

};  // ZEST_SUITE(serde_simdjson_config)

}  // namespace

}  // namespace kota::codec
