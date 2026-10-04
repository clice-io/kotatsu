#include <string>

#include "fixtures/attrs.h"
#include "fixtures/configs.h"
#include "kota/zest/zest.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"

namespace kota::meta {

namespace {

namespace fx = ::kota::test;

ZEST_SUITE(meta_schema_rename_all) {

ZEST_CASE(rename_policies) {
    // lower_camel
    {
        constexpr auto& fields = virtual_schema<fx::RenameAllTarget, fx::CamelConfig>::fields;
        ZSTATIC_EXPECT(fields[0].name == "userName");
        ZSTATIC_EXPECT(fields[1].name == "totalScore");
        ZSTATIC_EXPECT(fields[2].name == "itemId");
    }

    // upper_camel (PascalCase)
    {
        constexpr auto& fields = virtual_schema<fx::RenameAllTarget, fx::PascalConfig>::fields;
        ZSTATIC_EXPECT(fields[0].name == "UserName");
        ZSTATIC_EXPECT(fields[1].name == "TotalScore");
        ZSTATIC_EXPECT(fields[2].name == "ItemId");
    }

    // UPPER_SNAKE
    {
        constexpr auto& fields = virtual_schema<fx::RenameAllTarget, fx::UpperSnakeConfig>::fields;
        ZSTATIC_EXPECT(fields[0].name == "USER_NAME");
        ZSTATIC_EXPECT(fields[1].name == "TOTAL_SCORE");
        ZSTATIC_EXPECT(fields[2].name == "ITEM_ID");
    }

    // lower_snake (identity for already-snake_case)
    {
        constexpr auto& fields = virtual_schema<fx::RenameAllTarget, fx::LowerSnakeConfig>::fields;
        ZSTATIC_EXPECT(fields[0].name == "user_name");
        ZSTATIC_EXPECT(fields[1].name == "total_score");
        ZSTATIC_EXPECT(fields[2].name == "item_id");
    }

    // identity
    {
        constexpr auto& fields = virtual_schema<fx::RenameAllTarget, fx::IdentityConfig>::fields;
        ZSTATIC_EXPECT(fields[0].name == "user_name");
        ZSTATIC_EXPECT(fields[1].name == "total_score");
        ZSTATIC_EXPECT(fields[2].name == "item_id");
    }

    // default_config preserves names
    {
        constexpr auto& fields = virtual_schema<fx::RenameAllTarget, default_config>::fields;
        ZSTATIC_EXPECT(fields[0].name == "user_name");
        ZSTATIC_EXPECT(fields[1].name == "total_score");
        ZSTATIC_EXPECT(fields[2].name == "item_id");
    }
}

ZEST_CASE(explicit_rename_overrides_rename_all) {
    constexpr auto& fields = virtual_schema<fx::MixedRenameStruct, fx::CamelConfig>::fields;
    ZSTATIC_EXPECT(fields[0].name == "ID");          // explicit rename wins
    ZSTATIC_EXPECT(fields[1].name == "totalScore");  // rename_all applied
    ZSTATIC_EXPECT(fields[2].name == "itemName");    // rename_all applied
}

ZEST_CASE(field_count_unchanged) {
    ZSTATIC_EXPECT((virtual_schema<fx::RenameAllTarget, fx::CamelConfig>::count) == 3U);
    ZSTATIC_EXPECT((virtual_schema<fx::MixedRenameStruct, fx::CamelConfig>::count) == 3U);
}

ZEST_CASE(alias_unaffected_by_rename_all) {
    // Under camelCase rename_all, the canonical name changes but alias stays fixed
    constexpr auto& fields = virtual_schema<fx::AliasRenameAllStruct, fx::CamelConfig>::fields;

    // canonical name: "id" (reflection name, not renamed) -> camelCase -> "id" (single word)
    ZSTATIC_EXPECT(fields[0].name == "id");

    // Alias "user_id" stays verbatim
    ZSTATIC_EXPECT(fields[0].aliases.size() == 1U);
    ZSTATIC_EXPECT(fields[0].aliases[0] == "user_id");

    // Second field follows rename_all
    ZSTATIC_EXPECT(fields[1].name == "totalScore");
}

};  // ZEST_SUITE(meta_schema_rename_all)

}  // namespace

}  // namespace kota::meta
