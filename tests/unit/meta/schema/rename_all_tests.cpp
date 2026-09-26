#include <string>

#include "fixtures/schema/rename.h"
#include "kota/zest/zest.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"

namespace kota::meta {

namespace {

namespace fx = ::kota::meta::fixtures;

ZEST_SUITE(virtual_schema_rename_all){

    ZEST_CASE(rename_policies){
        // lower_camel
        {constexpr auto& fields = virtual_schema<fx::RenameAllTarget, fx::CamelConfig>::fields;
STATIC_EXPECT(fields[0].name == "userName");
STATIC_EXPECT(fields[1].name == "totalScore");
STATIC_EXPECT(fields[2].name == "itemId");

}  // namespace

// upper_camel (PascalCase)
{
    constexpr auto& fields = virtual_schema<fx::RenameAllTarget, fx::PascalConfig>::fields;
    STATIC_EXPECT(fields[0].name == "UserName");
    STATIC_EXPECT(fields[1].name == "TotalScore");
    STATIC_EXPECT(fields[2].name == "ItemId");
}

// UPPER_SNAKE
{
    constexpr auto& fields = virtual_schema<fx::RenameAllTarget, fx::UpperSnakeConfig>::fields;
    STATIC_EXPECT(fields[0].name == "USER_NAME");
    STATIC_EXPECT(fields[1].name == "TOTAL_SCORE");
    STATIC_EXPECT(fields[2].name == "ITEM_ID");
}

// lower_snake (identity for already-snake_case)
{
    constexpr auto& fields = virtual_schema<fx::RenameAllTarget, fx::LowerSnakeConfig>::fields;
    STATIC_EXPECT(fields[0].name == "user_name");
    STATIC_EXPECT(fields[1].name == "total_score");
    STATIC_EXPECT(fields[2].name == "item_id");
}

// identity
{
    constexpr auto& fields = virtual_schema<fx::RenameAllTarget, fx::IdentityConfig>::fields;
    STATIC_EXPECT(fields[0].name == "user_name");
    STATIC_EXPECT(fields[1].name == "total_score");
    STATIC_EXPECT(fields[2].name == "item_id");
}

// default_config preserves names
{
    constexpr auto& fields = virtual_schema<fx::RenameAllTarget, default_config>::fields;
    STATIC_EXPECT(fields[0].name == "user_name");
    STATIC_EXPECT(fields[1].name == "total_score");
    STATIC_EXPECT(fields[2].name == "item_id");
}

}  // namespace kota::meta

ZEST_CASE(explicit_rename_overrides_rename_all) {
    constexpr auto& fields = virtual_schema<fx::MixedRenameStruct, fx::CamelConfig>::fields;
    STATIC_EXPECT(fields[0].name == "ID");          // explicit rename wins
    STATIC_EXPECT(fields[1].name == "totalScore");  // rename_all applied
    STATIC_EXPECT(fields[2].name == "itemName");    // rename_all applied
}

ZEST_CASE(field_count_unchanged) {
    STATIC_EXPECT((virtual_schema<fx::RenameAllTarget, fx::CamelConfig>::count) == 3U);
    STATIC_EXPECT((virtual_schema<fx::MixedRenameStruct, fx::CamelConfig>::count) == 3U);
}

ZEST_CASE(alias_unaffected_by_rename_all) {
    // Under camelCase rename_all, the canonical name changes but alias stays fixed
    constexpr auto& fields = virtual_schema<fx::AliasRenameAllStruct, fx::CamelConfig>::fields;

    // canonical name: "id" (reflection name, not renamed) -> camelCase -> "id" (single word)
    STATIC_EXPECT(fields[0].name == "id");

    // Alias "user_id" stays verbatim
    STATIC_EXPECT(fields[0].aliases.size() == 1U);
    STATIC_EXPECT(fields[0].aliases[0] == "user_id");

    // Second field follows rename_all
    STATIC_EXPECT(fields[1].name == "totalScore");
}
}
;  // ZEST_SUITE(virtual_schema_rename_all)

}  // namespace

}  // namespace kota::meta
