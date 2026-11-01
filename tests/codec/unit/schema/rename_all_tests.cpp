#include <string>
#include <string_view>
#include <variant>

#include "fixtures/attrs.h"
#include "fixtures/settings.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/codec/attribute.h"
#include "kotatsu/codec/type_info.h"

namespace kotatsu::codec {

using meta::reflection;

namespace {

namespace fx = ::kotatsu::test;

/// A rename policy of the program's own: words joined by dashes.
struct Dashed {
    constexpr std::string operator()(std::string_view name) const {
        std::string out(name);
        for(auto& c: out) {
            if(c == '_') {
                c = '-';
            }
        }
        return out;
    }
};

// The types below are annotated as KANNOTATE_TYPE (in codec's macro sheet)
// expands to.

struct DashedFields;

constexpr auto __kotatsu__type_annotation(std::type_identity<DashedFields>) {
    return meta::make_annotation(dsl::rename_all = dsl::type<Dashed>);
}

struct DashedFields {
    int user_name = 0;
    int item_id = 0;
};

enum class DashedLevel { low_water, high_water };

constexpr auto __kotatsu__type_annotation(std::type_identity<DashedLevel>) {
    return meta::make_annotation(dsl::rename_all = dsl::type<Dashed>);
}

struct HoldsLevel {
    static consteval auto __kotatsu__annotation(meta::name_key<"level">) {
        return meta::make_annotation(dsl::with = dsl::type<enum_names<>>);
    }

    DashedLevel level = DashedLevel::low_water;
};

struct the_first {
    int a = 0;
};

struct the_second {
    int b = 0;
};

struct DashedTags : std::variant<the_first, the_second> {
    using variant::variant;
};

constexpr auto __kotatsu__type_annotation(std::type_identity<DashedTags>) {
    return meta::make_annotation(dsl::tagged = true, dsl::rename_all = dsl::type<Dashed>);
}

ZEST_SUITE(codec_schema_rename_all) {

ZEST_CASE(rename_policies) {
    // lower_camel
    {
        constexpr auto& fields = virtual_schema<fx::RenameAllTarget, fx::CamelSettings>::fields;
        ZSTATIC_EXPECT(fields[0].name == "userName");
        ZSTATIC_EXPECT(fields[1].name == "totalScore");
        ZSTATIC_EXPECT(fields[2].name == "itemId");
    }

    // upper_camel (PascalCase)
    {
        constexpr auto& fields = virtual_schema<fx::RenameAllTarget, fx::PascalSettings>::fields;
        ZSTATIC_EXPECT(fields[0].name == "UserName");
        ZSTATIC_EXPECT(fields[1].name == "TotalScore");
        ZSTATIC_EXPECT(fields[2].name == "ItemId");
    }

    // UPPER_SNAKE
    {
        constexpr auto& fields =
            virtual_schema<fx::RenameAllTarget, fx::UpperSnakeSettings>::fields;
        ZSTATIC_EXPECT(fields[0].name == "USER_NAME");
        ZSTATIC_EXPECT(fields[1].name == "TOTAL_SCORE");
        ZSTATIC_EXPECT(fields[2].name == "ITEM_ID");
    }

    // lower_snake (identity for already-snake_case)
    {
        constexpr auto& fields =
            virtual_schema<fx::RenameAllTarget, fx::LowerSnakeSettings>::fields;
        ZSTATIC_EXPECT(fields[0].name == "user_name");
        ZSTATIC_EXPECT(fields[1].name == "total_score");
        ZSTATIC_EXPECT(fields[2].name == "item_id");
    }

    // identity
    {
        constexpr auto& fields = virtual_schema<fx::RenameAllTarget, fx::IdentitySettings>::fields;
        ZSTATIC_EXPECT(fields[0].name == "user_name");
        ZSTATIC_EXPECT(fields[1].name == "total_score");
        ZSTATIC_EXPECT(fields[2].name == "item_id");
    }

    // codec::format preserves names
    {
        constexpr auto& fields = virtual_schema<fx::RenameAllTarget, codec::format>::fields;
        ZSTATIC_EXPECT(fields[0].name == "user_name");
        ZSTATIC_EXPECT(fields[1].name == "total_score");
        ZSTATIC_EXPECT(fields[2].name == "item_id");
    }
}

ZEST_CASE(explicit_rename_overrides_rename_all) {
    constexpr auto& fields = virtual_schema<fx::MixedRenameStruct, fx::CamelSettings>::fields;
    ZSTATIC_EXPECT(fields[0].name == "ID");          // explicit rename wins
    ZSTATIC_EXPECT(fields[1].name == "totalScore");  // rename_all applied
    ZSTATIC_EXPECT(fields[2].name == "itemName");    // rename_all applied
}

ZEST_CASE(field_count_unchanged) {
    ZSTATIC_EXPECT((virtual_schema<fx::RenameAllTarget, fx::CamelSettings>::fields.size()) == 3U);
    ZSTATIC_EXPECT((virtual_schema<fx::MixedRenameStruct, fx::CamelSettings>::fields.size()) == 3U);
}

ZEST_CASE(alias_unaffected_by_rename_all) {
    // Under camelCase rename_all, the canonical name changes but alias stays fixed
    constexpr auto& fields = virtual_schema<fx::AliasRenameAllStruct, fx::CamelSettings>::fields;

    // canonical name: "id" (reflection name, not renamed) -> camelCase -> "id" (single word)
    ZSTATIC_EXPECT(fields[0].name == "id");

    // Alias "user_id" stays verbatim
    ZSTATIC_EXPECT(fields[0].aliases.size() == 1U);
    ZSTATIC_EXPECT(fields[0].aliases[0] == "user_id");

    // Second field follows rename_all
    ZSTATIC_EXPECT(fields[1].name == "totalScore");
}

ZEST_CASE(rename_all_takes_a_policy_type) {
    constexpr auto& fields = virtual_schema<DashedFields>::fields;
    ZSTATIC_EXPECT(fields[0].name == "user-name");
    ZSTATIC_EXPECT(fields[1].name == "item-id");
    // The annotation's policy stands over the format's.
    ZSTATIC_EXPECT(virtual_schema<DashedFields, fx::CamelSettings>::fields[1].name == "item-id");
}

ZEST_CASE(policy_type_spells_enumerators_and_tags) {
    // Through enum_names, the enum's own rename_all spells its enumerators.
    const auto& level =
        static_cast<const enum_type_info&>(virtual_schema<HoldsLevel>::fields[0].type());
    ZEXPECT(level.named);
    ZEXPECT(level.member_names[1] == "high-water");

    const auto& tags = static_cast<const variant_type_info&>(type_info::of<DashedTags>());
    ZASSERT(tags.alt_names.size() == 2U);
    ZEXPECT(tags.alt_names[0] == "the-first");
    ZEXPECT(tags.alt_names[1] == "the-second");
}

};  // ZEST_SUITE(codec_schema_rename_all)

}  // namespace

}  // namespace kotatsu::codec
