#include <string>

#include "eventide/zest/zest.h"
#include "eventide/reflection/attrs.h"
#include "eventide/reflection/schema.h"

namespace eventide::refl {

namespace test_schema {

struct RenameAllTarget {
    int user_name;
    float total_score;
    std::string item_id;
};

struct CamelConfig {
    using field_rename = rename_policy::lower_camel;
};

struct PascalConfig {
    using field_rename = rename_policy::upper_camel;
};

struct UpperSnakeConfig {
    using field_rename = rename_policy::upper_snake;
};

struct LowerSnakeConfig {
    using field_rename = rename_policy::lower_snake;
};

struct IdentityConfig {
    using field_rename = rename_policy::identity;
};

// Struct with one field explicitly renamed + rename_all config
struct MixedRenameStruct {
    annotation<int, attrs::rename<"ID">> user_id;
    float total_score;
    std::string item_name;
};

// Struct with alias + rename_all: alias names should NOT be affected by rename_all
struct AliasRenameAllStruct {
    annotation<int, attrs::alias<"user_id">> id;
    float total_score;
};

}  // namespace test_schema

namespace {

TEST_SUITE(virtual_schema_rename_all) {

TEST_CASE(rename_policies) {
    // lower_camel
    {
        constexpr auto& fields =
            virtual_schema<test_schema::RenameAllTarget, test_schema::CamelConfig>::fields;
        EXPECT_EQ(fields[0].name, "userName");
        EXPECT_EQ(fields[1].name, "totalScore");
        EXPECT_EQ(fields[2].name, "itemId");
    }

    // upper_camel (PascalCase)
    {
        constexpr auto& fields =
            virtual_schema<test_schema::RenameAllTarget, test_schema::PascalConfig>::fields;
        EXPECT_EQ(fields[0].name, "UserName");
        EXPECT_EQ(fields[1].name, "TotalScore");
        EXPECT_EQ(fields[2].name, "ItemId");
    }

    // UPPER_SNAKE
    {
        constexpr auto& fields =
            virtual_schema<test_schema::RenameAllTarget, test_schema::UpperSnakeConfig>::fields;
        EXPECT_EQ(fields[0].name, "USER_NAME");
        EXPECT_EQ(fields[1].name, "TOTAL_SCORE");
        EXPECT_EQ(fields[2].name, "ITEM_ID");
    }

    // lower_snake (identity for already-snake_case)
    {
        constexpr auto& fields =
            virtual_schema<test_schema::RenameAllTarget, test_schema::LowerSnakeConfig>::fields;
        EXPECT_EQ(fields[0].name, "user_name");
        EXPECT_EQ(fields[1].name, "total_score");
        EXPECT_EQ(fields[2].name, "item_id");
    }

    // identity
    {
        constexpr auto& fields =
            virtual_schema<test_schema::RenameAllTarget, test_schema::IdentityConfig>::fields;
        EXPECT_EQ(fields[0].name, "user_name");
        EXPECT_EQ(fields[1].name, "total_score");
        EXPECT_EQ(fields[2].name, "item_id");
    }

    // default_config preserves names
    {
        constexpr auto& fields =
            virtual_schema<test_schema::RenameAllTarget, default_config>::fields;
        EXPECT_EQ(fields[0].name, "user_name");
        EXPECT_EQ(fields[1].name, "total_score");
        EXPECT_EQ(fields[2].name, "item_id");
    }
}

TEST_CASE(explicit_rename_overrides_rename_all) {
    constexpr auto& fields =
        virtual_schema<test_schema::MixedRenameStruct, test_schema::CamelConfig>::fields;
    EXPECT_EQ(fields[0].name, "ID");          // explicit rename wins
    EXPECT_EQ(fields[1].name, "totalScore");  // rename_all applied
    EXPECT_EQ(fields[2].name, "itemName");    // rename_all applied
}

TEST_CASE(field_count_unchanged) {
    EXPECT_EQ((virtual_schema<test_schema::RenameAllTarget, test_schema::CamelConfig>::count), 3U);
    EXPECT_EQ((virtual_schema<test_schema::MixedRenameStruct, test_schema::CamelConfig>::count),
              3U);
}

TEST_CASE(alias_unaffected_by_rename_all) {
    // Under camelCase rename_all, the canonical name changes but alias stays fixed
    constexpr auto& fields =
        virtual_schema<test_schema::AliasRenameAllStruct, test_schema::CamelConfig>::fields;

    // canonical name: "id" (reflection name, not renamed) -> camelCase -> "id" (single word)
    EXPECT_EQ(fields[0].name, "id");

    // Alias "user_id" stays verbatim
    EXPECT_EQ(fields[0].aliases.size(), 1U);
    EXPECT_EQ(fields[0].aliases[0], "user_id");

    // Second field follows rename_all
    EXPECT_EQ(fields[1].name, "totalScore");
}

};  // TEST_SUITE(virtual_schema_rename_all)

}  // namespace

}  // namespace eventide::refl
