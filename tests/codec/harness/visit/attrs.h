#pragma once

// Attrs and config: how field and struct annotations, the behavior attrs and
// the config knobs shape the document, and the protocol's errors for keyed
// documents.

#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "codec/harness/fixtures/configs.h"
#include "codec/harness/visit/kit.h"
#include "fixtures/attrs.h"
#include "fixtures/configs.h"
#include "fixtures/containers.h"
#include "fixtures/enums.h"
#include "fixtures/scalars.h"
#include "fixtures/structs.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"

namespace kota::test {

template <Backend B>
void attrs(const Kit<B>& kit) {
    using Ints = std::map<std::string, int>;

    encodes_as(
        kit,
        "rename_and_skip_encode_as_plain",
        [] { return AnnotatedStruct{.user_id = 7, .internal = "hidden", .value = 2.5F}; },
        [] { return AnnotatedPlain{.id = 7, .value = 2.5F}; });
    // Decoding leaves a skipped field as the value it decodes into holds it.
    reads<AnnotatedStruct>(
        kit,
        "skip_leaves_field_alone",
        [] { return AnnotatedPlain{.id = 7, .value = 2.5F}; },
        [] { return AnnotatedStruct{.user_id = 7, .internal = "kept", .value = 2.5F}; });
    roundtrip(kit, "skip_needs_no_codec_for_its_type", [] {
        return SkipsRawPointer{.id = 7, .raw = nullptr};
    });

    encodes_as(
        kit,
        "rename_all_encodes_as_plain",
        [] {
            return RenamedRoot{
                {.user_name = 7, .display_name = "ada"}
            };
        },
        [] { return RenameTargetCamel{.userName = 7, .displayName = "ada"}; });
    roundtrip(kit, "rename_all_roundtrip", [] {
        return RenamedRoot{
            {.user_name = 7, .display_name = "ada"}
        };
    });
    encodes_as(
        kit,
        "rename_all_on_field_encodes_as_plain",
        [] {
            return Field<meta::annotate<UpperSnakeStrictTag>::type<RenameTarget>>{
                {{.user_name = 7, .display_name = "ada"}}};
        },
        [] {
            return Field<RenameTargetUpperSnake>{
                {.USER_NAME = 7, .DISPLAY_NAME = "ada"}
            };
        });
    roundtrip(kit, "rename_all_on_field_roundtrip", [] {
        return Field<meta::annotate<UpperSnakeStrictTag>::type<RenameTarget>>{
            {{.user_name = 7, .display_name = "ada"}}};
    });
    encodes_as<CamelConfig>(
        kit,
        "field_rename_encodes_as_plain",
        [] { return RenameAllTarget{.user_name = 7, .total_score = 1.5F, .item_id = "abc"}; },
        [] { return RenameAllTargetCamel{.userName = 7, .totalScore = 1.5F, .itemId = "abc"}; });
    encodes_as<CamelConfig>(
        kit,
        "field_rename_reaches_nested_structs",
        [] {
            return NestedRenameTarget{
                .request_id = 1,
                .nested_info = {.user_name = 7, .total_score = 1.5F, .item_id = "abc"},
            };
        },
        [] {
            return NestedRenameTargetCamel{
                .requestId = 1,
                .nestedInfo = {.userName = 7, .totalScore = 1.5F, .itemId = "abc"},
            };
        });
    roundtrip<CamelConfig>(kit, "field_rename_roundtrip", [] {
        return NestedRenameTarget{
            .request_id = 1,
            .nested_info = {.user_name = 7, .total_score = 1.5F, .item_id = "abc"},
        };
    });
    encodes_as<CamelConfig>(
        kit,
        "rename_beats_field_rename",
        [] { return MixedRenameStruct{.user_id = 7, .total_score = 1.5F, .item_name = "x"}; },
        [] { return MixedRenameStructCamel{.ID = 7, .totalScore = 1.5F, .itemName = "x"}; });
    reads<MixedRenameStruct, CamelConfig>(
        kit,
        "rename_beats_field_rename_reads",
        [] { return MixedRenameStructCamel{.ID = 7, .totalScore = 1.5F, .itemName = "x"}; },
        [] { return MixedRenameStruct{.user_id = 7, .total_score = 1.5F, .item_name = "x"}; });

    encodes_as(
        kit,
        "flatten_encodes_as_plain",
        [] { return Outer{.x = 1, .inner = {{.a = 2, .b = 3}}, .y = 4}; },
        [] { return OuterPlain{.x = 1, .a = 2, .b = 3, .y = 4}; });
    reads<Outer>(
        kit,
        "flatten_reads",
        [] { return OuterPlain{.x = 1, .a = 2, .b = 3, .y = 4}; },
        [] { return Outer{.x = 1, .inner = {{.a = 2, .b = 3}}, .y = 4}; });
    encodes_as(
        kit,
        "deep_flatten_encodes_as_plain",
        [] {
            return DeepOuter{.head = 1, .mid = {{.m = 2, .deep = {{.p = 3, .q = 4}}}}, .tail = 5};
        },
        [] { return DeepOuterPlain{.head = 1, .m = 2, .p = 3, .q = 4, .tail = 5}; });
    reads<DeepOuter>(
        kit,
        "deep_flatten_reads",
        [] { return DeepOuterPlain{.head = 1, .m = 2, .p = 3, .q = 4, .tail = 5}; },
        [] {
            return DeepOuter{.head = 1, .mid = {{.m = 2, .deep = {{.p = 3, .q = 4}}}}, .tail = 5};
        });
    encodes_as(
        kit,
        "flatten_keeps_inner_rename",
        [] { return FlattenOuterWithChildRename{{{.a = 1, .b = 2}}}; },
        [] { return FlattenOuterWithChildRenamePlain{.renamed_a = 1, .b = 2}; });
    reads<FlattenOuterWithChildRename>(
        kit,
        "flatten_keeps_inner_rename_reads",
        [] { return FlattenOuterWithChildRenamePlain{.renamed_a = 1, .b = 2}; },
        [] { return FlattenOuterWithChildRename{{{.a = 1, .b = 2}}}; });
    encodes_as(
        kit,
        "flatten_keeps_inner_skip",
        [] {
            return FlattenOuterWithChildSkip{
                .head = 1,
                .inner = {{.keep_a = 2, .drop_b = 9, .keep_c = 3}},
            };
        },
        [] { return FlattenOuterWithChildSkipPlain{.head = 1, .keep_a = 2, .keep_c = 3}; });
    reads<FlattenOuterWithChildSkip>(
        kit,
        "flatten_keeps_inner_skip_reads",
        [] { return FlattenOuterWithChildSkipPlain{.head = 1, .keep_a = 2, .keep_c = 3}; },
        [] {
            return FlattenOuterWithChildSkip{
                .head = 1,
                .inner = {{.keep_a = 2, .drop_b = 0, .keep_c = 3}},
            };
        });

    encodes_as(
        kit,
        "as_encodes_as_plain",
        [] {
            return AsTargets{
                .owner = UserId("ada"),
                .samples = Samples({1, 2}),
                .cell = GridIndex(Point{.x = 3, .y = 4}),
            };
        },
        [] {
            return AsTargetsPlain{
                .owner = "ada",
                .samples = {1,      2     },
                .cell = {.x = 3, .y = 4}
            };
        });
    roundtrip(kit, "as_roundtrip", [] {
        return AsTargets{
            .owner = UserId("ada"),
            .samples = Samples({1, 2}),
            .cell = GridIndex(Point{.x = 3, .y = 4}),
        };
    });
    using Decimal = meta::annotation<int, meta::behavior::with<DecimalText>>;
    encodes_as(
        kit,
        "with_encodes_as_plain",
        [] { return Field<Decimal>{42}; },
        [] { return Field<std::string>{"42"}; });
    roundtrip(kit, "with_roundtrip", [] { return Field<Decimal>{42}; });
    encodes_as(
        kit,
        "enum_string_encodes_as_plain",
        [] { return AccessGrant{.level = Access::read_only, .count = 2}; },
        [] { return AccessGrantPlain{.level = "readOnly", .count = 2}; });
    roundtrip(kit, "enum_string_roundtrip", [] {
        return AccessGrant{.level = Access::full_control, .count = 2};
    });
    encodes_as(
        kit,
        "enum_string_root_encodes_as_plain",
        [] { return AccessName{Access::full_control}; },
        [] { return std::string("fullControl"); });
    roundtrip(kit, "enum_string_root_roundtrip", [] { return AccessName{Access::full_control}; });
    encodes_as(
        kit,
        "description_is_transparent",
        [] { return Documented{.id = 7, .name = "ada"}; },
        [] { return StrictIdName{.id = 7, .name = "ada"}; });
    roundtrip(kit, "description_roundtrip", [] { return Documented{.id = 7, .name = "ada"}; });

    if constexpr(B::caps.absent_fields) {
        encodes_as(
            kit,
            "skip_if_omits_matching_fields",
            [] {
                return Skippable{
                    .id = 1,
                    .note = std::nullopt,
                    .tags = {},
                    .generation = 0,
                    .score = -5,
                };
            },
            [] { return IdOnly{.id = 1}; });
        encodes_as(
            kit,
            "skip_if_keeps_other_fields",
            [] {
                return Skippable{
                    .id = 1,
                    .note = "n",
                    .tags = {1, 2},
                    .generation = 5,
                    .score = 7
                };
            },
            [] {
                return SkippablePlain{
                    .id = 1,
                    .note = "n",
                    .tags = {1, 2},
                    .generation = 5,
                    .score = 7
                };
            });
        reads<Skippable>(
            kit,
            "skip_if_absent_fields_read_default",
            [] { return IdOnly{.id = 1}; },
            [] {
                return Skippable{.id = 1,
                                 .note = std::nullopt,
                                 .tags = {},
                                 .generation = 0,
                                 .score = 0};
            });
        roundtrip(kit, "skip_if_roundtrip", [] {
            return Skippable{
                .id = 1,
                .note = "n",
                .tags = {1, 2},
                .generation = 5,
                .score = 7
            };
        });
    }

    if constexpr(B::caps.string_knobs) {
        encodes_as<EnumStringConfig>(
            kit,
            "enum_repr_string_encodes_as_plain",
            [] { return Access::read_only; },
            [] { return std::string("read_only"); });
        roundtrip<EnumStringConfig>(kit, "enum_repr_string_roundtrip", [] {
            return Access::full_control;
        });
        encodes_as<EnumRenameConfig>(
            kit,
            "enum_rename_encodes_as_plain",
            [] { return Access::read_only; },
            [] { return std::string("readOnly"); });
        roundtrip<EnumRenameConfig>(kit, "enum_rename_roundtrip", [] {
            return Access::full_control;
        });
        write_fails<EnumStringConfig>(
            kit,
            "enum_repr_string_unnamed_value_fails",
            [] { return Field<Access>{static_cast<Access>(9)}; },
            {.message = "enum value 9 has no reflected name", .path = "value"});
        encodes_as<NanStringConfig>(
            kit,
            "nan_string_encodes_as_names",
            [] { return NonFinite::typical(); },
            [] { return NonFiniteNames{.nan = "NaN", .inf = "Infinity", .neg_inf = "-Infinity"}; });
    }
    if constexpr(B::caps.non_finite) {
        roundtrip(kit, "infinity_roundtrip", [] {
            return std::vector<double>{std::numeric_limits<double>::infinity(),
                                       -std::numeric_limits<double>::infinity()};
        });
    } else {
        encodes_as(
            kit,
            "non_finite_encodes_as_null",
            [] { return NonFinite::typical(); },
            [] { return NonFiniteNulls{}; });
    }
    encodes_as<NanNullConfig>(
        kit,
        "nan_null_encodes_as_null",
        [] { return NonFinite::typical(); },
        [] { return NonFiniteNulls{}; });
    write_fails<NanErrorConfig>(kit,
                                "nan_error_fails",
                                [] { return NonFinite::typical(); },
                                {.message = "NaN or Infinity is not allowed", .path = "nan"});

    if constexpr(B::caps.self_describing) {
        reads<AliasStruct>(
            kit,
            "alias_reads",
            [] { return AliasPlain{.userId = 7, .name = "ada"}; },
            [] { return AliasStruct{.id = 7, .name = "ada"}; });
        // The first field takes the value; the second is left missing.
        read_fails<SharedAlias>(kit,
                                "alias_shared_by_two_fields_fails",
                                [] {
                                    return Ints{
                                        {"dup", 1}
                                    };
                                },
                                {.message = "missing required field 'right'", .path = ""});
        read_fails<CamelCollision, CamelConfig>(
            kit,
            "rename_collision_fails",
            [] {
                return Ints{
                    {"userId", 1}
                };
            },
            {.message = "missing required field 'userId'", .path = ""});
        reads<DefaultStruct>(
            kit,
            "defaulted_absent_reads_default",
            [] { return DefaultStructAbsent{.version = "v1", .plain = 2}; },
            [] { return DefaultStruct{.with_default = 3, .version = "v1", .plain = 2}; });
        reads<DefaultStruct>(
            kit,
            "defaulted_present_reads",
            [] { return DefaultStructPlain{.with_default = 9, .version = "v1", .plain = 2}; },
            [] { return DefaultStruct{.with_default = 9, .version = "v1", .plain = 2}; });
        reads<Nullables>(
            kit,
            "nullable_fields_may_be_absent",
            [] { return Empty{}; },
            [] { return Nullables{}; });
        read_fails<DefaultStruct>(kit,
                                  "missing_field_fails",
                                  [] {
                                      return Ints{
                                          {"plain", 2}
                                      };
                                  },
                                  {.message = "missing required field 'version'", .path = ""});
        reads<Point>(
            kit,
            "unknown_field_ignored",
            [] {
                return Ints{
                    {"x",     1},
                    {"y",     2},
                    {"extra", 3}
                };
            },
            [] { return Point{.x = 1, .y = 2}; });
        read_fails<StrictRoot>(
            kit,
            "unknown_field_fails",
            [] { return RenameTargetWithExtra{.user_name = 7, .display_name = "ada", .extra = 1}; },
            {.message = "unknown field 'extra'", .path = ""});
        read_fails<Point, StrictConfig>(kit,
                                        "unknown_field_fails_under_config",
                                        [] {
                                            return Ints{
                                                {"x",     1},
                                                {"y",     2},
                                                {"extra", 3}
                                            };
                                        },
                                        {.message = "unknown field 'extra'", .path = ""});
        read_fails<Field<meta::annotate<UpperSnakeStrictTag>::type<RenameTarget>>>(
            kit,
            "rename_all_on_field_denies_unknown_fails",
            [] {
                return Field<RenameTargetUpperSnakeWithExtra>{
                    {.USER_NAME = 7, .DISPLAY_NAME = "ada", .EXTRA = 1}
                };
            },
            {.message = "unknown field 'EXTRA'", .path = "value"});
        encodes_as(
            kit,
            "rename_all_on_untagged_variant_is_inert",
            [] {
                return CamelChoice{
                    RenameTarget{.user_name = 7, .display_name = "ada"}
                };
            },
            [] { return RenameTarget{.user_name = 7, .display_name = "ada"}; });
        reads<AnnotatedStruct>(
            kit,
            "skipped_field_ignores_its_key",
            [] { return AnnotatedWithInternal{.id = 7, .internal = "input", .value = 2.5F}; },
            [] { return AnnotatedStruct{.user_id = 7, .internal = "kept", .value = 2.5F}; });

        if constexpr(B::caps.string_knobs) {
            read_fails<Field<Access>, EnumStringConfig>(
                kit,
                "enum_repr_string_unknown_name_fails",
                [] { return Field<std::string>{"nope"}; },
                {.message = "unknown enum value 'nope'", .path = "value"});
            // nan_repr::String only encodes: the names do not read back.
            read_fails<NonFinite, NanStringConfig>(
                kit,
                "nan_string_read_fails",
                [] {
                    return NonFiniteNames{.nan = "NaN", .inf = "Infinity", .neg_inf = "-Infinity"};
                },
                {.message = "", .path = "nan"});
        }
        read_fails<AccessGrant>(kit,
                                "enum_string_unknown_name_fails",
                                [] { return AccessGrantPlain{.level = "super_admin", .count = 1}; },
                                {.message = "unknown enum value 'super_admin'", .path = "level"});
        read_fails<AccessName>(kit,
                               "enum_string_root_unknown_name_fails",
                               [] { return std::string("super_admin"); },
                               {.message = "unknown enum value 'super_admin'", .path = ""});

        // Leaf errors are the backend's to word; their paths are the protocol's.
        using Texts = std::map<std::string, std::string>;
        read_fails<Field<Point>>(kit,
                                 "nested_field_mismatch_fails",
                                 [] {
                                     return Field<Texts>{
                                         {{"x", "one"}, {"y", "two"}}
                                     };
                                 },
                                 {.message = "", .path = "value.x"});
        read_fails<Field<Point>, NoPathConfig>(kit,
                                               "detailed_error_off_has_no_path",
                                               [] {
                                                   return Field<Texts>{
                                                       {{"x", "one"}, {"y", "two"}}
                                                   };
                                               },
                                               {.message = "", .path = ""});
        using IntOrText = std::variant<int, std::string>;
        read_fails<Field<std::vector<int>>>(kit,
                                            "element_mismatch_fails",
                                            [] {
                                                return Field<std::vector<IntOrText>>{
                                                    {1, "two", 3}
                                                };
                                            },
                                            {.message = "", .path = "value[1]"});
        read_fails<Field<Ints>>(kit,
                                "map_value_mismatch_fails",
                                [] {
                                    return Field<std::map<std::string, IntOrText>>{
                                        {{"a", 1}, {"b", "two"}}
                                    };
                                },
                                {.message = "", .path = "value[1]"});
        read_fails<Field<std::map<int, int>>>(
            kit,
            "map_key_not_integer_fails",
            [] { return Field<Ints>{{{"abc", 1}}}; },
            {.message = "cannot parse map key 'abc' as integer", .path = "value[0]"});
        read_fails<Field<std::map<std::uint32_t, int>>>(
            kit,
            "map_key_negative_unsigned_fails",
            [] { return Field<Ints>{{{"-1", 1}}}; },
            {.message = "cannot parse map key '-1' as unsigned integer", .path = "value[0]"});
        read_fails<Field<std::map<std::int8_t, int>>>(
            kit,
            "map_key_out_of_integer_range_fails",
            [] {
                return Field<Ints>{
                    {{"1", 1}, {"300", 2}}
                };
            },
            {.message = "map key '300' out of integer range", .path = "value[1]"});
        read_fails<Field<std::map<std::uint8_t, int>>>(
            kit,
            "map_key_out_of_unsigned_range_fails",
            [] {
                return Field<Ints>{
                    {{"1", 1}, {"300", 2}}
                };
            },
            {.message = "map key '300' out of unsigned integer range", .path = "value[1]"});
        read_fails<Field<std::tuple<int, int>>>(
            kit,
            "tuple_too_long_fails",
            [] {
                return Field<std::vector<int>>{
                    {1, 2, 3}
                };
            },
            {.message = "too many elements for tuple (expected 2)", .path = "value"});
        read_fails<Field<std::tuple<int, int>>>(
            kit,
            "tuple_too_short_fails",
            [] { return Field<std::vector<int>>{{1}}; },
            {.message = "too few elements for tuple (expected 2, got 1)", .path = "value"});
        read_fails<Field<std::tuple<>>>(
            kit,
            "empty_tuple_too_long_fails",
            [] { return Field<std::vector<int>>{{1}}; },
            {.message = "too many elements for tuple (expected 0)", .path = "value"});
        read_fails<Field<std::tuple<int, int>>>(kit,
                                                "tuple_element_mismatch_fails",
                                                [] {
                                                    return Field<std::tuple<int, std::string>>{
                                                        {1, "two"}
                                                    };
                                                },
                                                {.message = "", .path = "value[1]"});
    }
}

}  // namespace kota::test
