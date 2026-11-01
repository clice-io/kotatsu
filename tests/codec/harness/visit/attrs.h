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

#include "codec/harness/fixtures/attrs.h"
#include "codec/harness/fixtures/configs.h"
#include "codec/harness/fixtures/containers.h"
#include "codec/harness/fixtures/enums.h"
#include "codec/harness/fixtures/scalars.h"
#include "codec/harness/fixtures/structs.h"
#include "codec/harness/visit/kit.h"
#include "fixtures/attrs.h"
#include "fixtures/configs.h"

namespace kotatsu::test {

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

    // A positional document carries the same values in the same order
    // whatever they are called, so names are checked where they travel, in a
    // keyed document, and the roundtrips run everywhere.
    auto renamed_root = [] {
        return RenamedRoot{.user_name = 7, .display_name = "ada"};
    };
    roundtrip(kit, "rename_all_roundtrip", renamed_root);
    auto policy_on_field = [] {
        return SnakeStrict{
            {.user_name = 7, .display_name = "ada"}
        };
    };
    roundtrip(kit, "rename_all_on_field_roundtrip", policy_on_field);
    auto nested_rename = [] {
        return NestedRenameTarget{
            .request_id = 1,
            .nested_info = {.user_name = 7, .total_score = 1.5F, .item_id = "abc"},
        };
    };
    roundtrip<CamelConfig>(kit, "field_rename_roundtrip", nested_rename);
    auto documented = [] {
        return Documented{.id = 7, .name = "ada"};
    };
    roundtrip(kit, "description_roundtrip", documented);
    if constexpr(B::caps.self_describing) {
        encodes_as(kit, "rename_all_encodes_as_plain", renamed_root, [] {
            return RenameTargetCamel{.userName = 7, .displayName = "ada"};
        });
        encodes_as(kit, "rename_all_on_field_encodes_as_plain", policy_on_field, [] {
            return Field<RenameTargetUpperSnake>{
                {.USER_NAME = 7, .DISPLAY_NAME = "ada"}
            };
        });
        encodes_as<CamelConfig>(
            kit,
            "field_rename_encodes_as_plain",
            [] { return RenameAllTarget{.user_name = 7, .total_score = 1.5F, .item_id = "abc"}; },
            [] {
                return RenameAllTargetCamel{.userName = 7, .totalScore = 1.5F, .itemId = "abc"};
            });
        encodes_as<CamelConfig>(kit, "field_rename_reaches_nested_structs", nested_rename, [] {
            return NestedRenameTargetCamel{
                .requestId = 1,
                .nestedInfo = {.userName = 7, .totalScore = 1.5F, .itemId = "abc"},
            };
        });
        auto mixed_rename = [] {
            return MixedRenameStruct{.user_id = 7, .total_score = 1.5F, .item_name = "x"};
        };
        auto mixed_rename_plain = [] {
            return MixedRenameStructCamel{.ID = 7, .totalScore = 1.5F, .itemName = "x"};
        };
        encodes_as<CamelConfig>(kit, "rename_beats_field_rename", mixed_rename, mixed_rename_plain);
        reads<MixedRenameStruct, CamelConfig>(kit,
                                              "rename_beats_field_rename_reads",
                                              mixed_rename_plain,
                                              mixed_rename);
        encodes_as(kit, "description_is_transparent", documented, [] {
            return StrictIdName{.id = 7, .name = "ada"};
        });
    }

    // Flattening shows wherever a nested struct is framed: in a keyed
    // document and in fbs's tables. bincode concatenates a nested struct's
    // fields either way, so there these cases hold whatever flatten does.
    auto outer = [] {
        return Outer{
            .x = 1,
            .inner = {.a = 2, .b = 3},
            .y = 4
        };
    };
    auto outer_plain = [] {
        return OuterPlain{.x = 1, .a = 2, .b = 3, .y = 4};
    };
    encodes_as(kit, "flatten_encodes_as_plain", outer, outer_plain);
    reads<Outer>(kit, "flatten_reads", outer_plain, outer);
    auto deep = [] {
        return DeepOuter{
            .head = 1,
            .mid = {.m = 2, .deep = {.p = 3, .q = 4}},
            .tail = 5
        };
    };
    auto deep_plain = [] {
        return DeepOuterPlain{.head = 1, .m = 2, .p = 3, .q = 4, .tail = 5};
    };
    encodes_as(kit, "deep_flatten_encodes_as_plain", deep, deep_plain);
    reads<DeepOuter>(kit, "deep_flatten_reads", deep_plain, deep);
    auto inner_rename = [] {
        return FlattenOuterWithChildRename{
            {.a = 1, .b = 2}
        };
    };
    auto inner_rename_plain = [] {
        return FlattenOuterWithChildRenamePlain{.renamed_a = 1, .b = 2};
    };
    encodes_as(kit, "flatten_keeps_inner_rename", inner_rename, inner_rename_plain);
    reads<FlattenOuterWithChildRename>(kit,
                                       "flatten_keeps_inner_rename_reads",
                                       inner_rename_plain,
                                       inner_rename);
    auto inner_skip_plain = [] {
        return FlattenOuterWithChildSkipPlain{.head = 1, .keep_a = 2, .keep_c = 3};
    };
    encodes_as(
        kit,
        "flatten_keeps_inner_skip",
        [] {
            return FlattenOuterWithChildSkip{
                .head = 1,
                .inner = {.keep_a = 2, .drop_b = 9, .keep_c = 3},
            };
        },
        inner_skip_plain);
    reads<FlattenOuterWithChildSkip>(kit, "flatten_keeps_inner_skip_reads", inner_skip_plain, [] {
        return FlattenOuterWithChildSkip{
            .head = 1,
            .inner = {.keep_a = 2, .drop_b = 0, .keep_c = 3},
        };
    });

    auto as_targets = [] {
        return AsTargets{
            .owner = UserId("ada"),
            .samples = Samples({1, 2}),
            .cell = GridIndex(Point{.x = 3, .y = 4}),
        };
    };
    encodes_as(kit, "as_encodes_as_plain", as_targets, [] {
        return AsTargetsPlain{
            .owner = "ada",
            .samples = {1,      2     },
            .cell = {.x = 3, .y = 4}
        };
    });
    roundtrip(kit, "as_roundtrip", as_targets);
    // The target is value-initialized before it is read: the explicit default
    // constructor of one of its members rejects `{}`.
    roundtrip(kit, "as_value_initialized_target_roundtrip", [] {
        return TallyAsHeld{Tally(std::vector<int>{1, 2})};
    });
    auto decimal = [] {
        return Decimal{42};
    };
    encodes_as(kit, "with_encodes_as_plain", decimal, [] { return Field<std::string>{"42"}; });
    roundtrip(kit, "with_roundtrip", decimal);
    auto renamed_decimal = [] {
        return RenamedDecimal{.value = 42};
    };
    encodes_as(kit, "rename_and_with_encode_as_both_say", renamed_decimal, [] {
        return RenamedDecimalPlain{.amount = "42"};
    });
    roundtrip(kit, "rename_and_with_roundtrip", renamed_decimal);
    auto grant = [] {
        return AccessGrant{.level = Access::read_only, .count = 2};
    };
    encodes_as(kit, "enum_string_encodes_as_plain", grant, [] {
        return AccessGrantPlain{.level = "readOnly", .count = 2};
    });
    roundtrip(kit, "enum_string_roundtrip", grant);
    write_fails(kit,
                "enum_string_unnamed_value_fails",
                [] { return AccessName{static_cast<Access>(9)}; },
                {.message = "enum value 9 has no reflected name", .path = "value"});
    // The values a write leaves out: each field at its default, but the
    // score, which its predicate judges.
    auto matching = [] {
        return Omittable{
            .id = 1,
            .note = "kept",
            .tags = {9},
            .generation = 7,
            .score = -5,
        };
    };
    // A predicate judges only the value being written: a decode reads the
    // field whatever the value it decodes into holds, here the empty text the
    // predicate matches.
    reads<OmitsEmptyText>(
        kit,
        "omit_if_reads_its_field",
        [] { return TextPlain{.text = "x"}; },
        [] { return OmitsEmptyText{.text = std::string("x")}; });
    if constexpr(B::caps.absent_fields) {
        // ...and, encoding, it leaves out the value it matches.
        encodes_as(
            kit,
            "omit_if_leaves_out_matching_field",
            [] { return OmitsEmptyText{.text = std::string()}; },
            [] { return Empty{}; });
        auto kept = [] {
            return Omittable{
                .id = 1,
                .note = "n",
                .tags = {1, 2},
                .generation = 5,
                .score = 7
            };
        };
        encodes_as(kit, "presence_omits_matching_fields", matching, [] { return IdOnly{.id = 1}; });
        encodes_as(kit, "presence_keeps_other_fields", kept, [] {
            return OmittablePlain{
                .id = 1,
                .note = "n",
                .tags = {1, 2},
                .generation = 5,
                .score = 7
            };
        });
        // A field's default is its initializer, not the empty value: an
        // empty value that is not the default is written.
        encodes_as(
            kit,
            "omit_if_default_compares_with_the_initializer",
            [] {
                return Omittable{
                    .id = 1,
                    .note = std::nullopt,
                    .tags = {},
                    .generation = 0,
                    .score = 3,
                };
            },
            [] {
                return OmittablePlain{
                    .id = 1,
                    .note = std::nullopt,
                    .tags = {},
                    .generation = 0,
                    .score = 3,
                };
            });
        // The default is the field of a value-initialized struct, here one
        // `{}` cannot make.
        encodes_as(
            kit,
            "omit_if_default_value_initializes_its_struct",
            [] { return CountBesideExplicit{.list = ExplicitList{1}, .count = 2}; },
            [] { return ListPlain{.list = {1}}; });
        // A keyed decode never visits an absent field, which is left as the
        // value it decodes into holds it; what a slot decode reads for an
        // absent slot is the backend's own.
        if constexpr(B::caps.self_describing) {
            reads<Omittable>(
                kit,
                "presence_absent_fields_left_alone",
                [] { return IdOnly{.id = 1}; },
                [] {
                    return Omittable{.id = 1,
                                     .note = "kept",
                                     .tags = {9},
                                     .generation = 7,
                                     .score = 3};
                });
            // So a field left out at its default reads back as written.
            roundtrip(kit, "omit_if_default_at_its_default_roundtrip", [] {
                return Omittable{.id = 1};
            });
        }
        roundtrip(kit, "presence_roundtrip", kept);
    } else {
        // Nothing can mark a field absent, so presence omits nothing: a field
        // it would leave out is written, and reads back as written.
        encodes_as(kit, "presence_writes_matching_fields", matching, [] {
            return OmittablePlain{
                .id = 1,
                .note = "kept",
                .tags = {9},
                .generation = 7,
                .score = -5,
            };
        });
        roundtrip(kit, "presence_matching_fields_roundtrip", matching);
    }

    // omit_unstated_defaults leaves out a schema_default = false field where
    // a field can be absent, and writes it where every field is written.
    auto unstated = [] {
        return WithUnstated{.id = 1, .seed = 7};
    };
    if constexpr(B::caps.absent_fields) {
        encodes_as<OmitUnstatedConfig>(kit,
                                       "omit_unstated_defaults_leaves_out_unstated_fields",
                                       unstated,
                                       [] { return IdOnly{.id = 1}; });
    } else {
        roundtrip<OmitUnstatedConfig>(kit, "omit_unstated_defaults_writes_every_field", unstated);
    }

    if constexpr(!B::caps.layout_computed) {
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
    // nan_repr::Null shows only where Passthrough keeps the values; where
    // Passthrough already writes null, the two configs write alike.
    if constexpr(B::caps.non_finite) {
        // In fields rather than an array: toml++ 3.4.0 lays an array out by
        // the log10 of each float, which is undefined for an infinity.
        roundtrip(kit, "infinity_roundtrip", [] { return Infinities::typical(); });
        encodes_as<NanNullConfig>(
            kit,
            "nan_null_encodes_as_null",
            [] { return NonFinite::typical(); },
            [] { return NonFiniteNulls{}; });
    } else {
        encodes_as(
            kit,
            "non_finite_encodes_as_null",
            [] { return NonFinite::typical(); },
            [] { return NonFiniteNulls{}; });
    }
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
            "optional_absent_keeps_its_value",
            [] { return DefaultStructAbsent{.version = "v1", .plain = 2}; },
            [] { return DefaultStruct{.with_default = 3, .version = "v1", .plain = 2}; });
        reads<DefaultStruct>(
            kit,
            "optional_present_reads",
            [] { return DefaultStructPlain{.with_default = 9, .version = "v1", .plain = 2}; },
            [] { return DefaultStruct{.with_default = 9, .version = "v1", .plain = 2}; });
        // Under presence_all = optional every field may be absent, in the
        // structs nested inside too, and keeps its value.
        using Sections = std::map<std::string, Ints>;
        reads<Tunables, AllOptionalConfig>(
            kit,
            "presence_all_config_reads_what_is_there",
            [] {
                return Sections{
                    {"limits", {{"high", 20}}}
                };
            },
            [] {
                return Tunables{
                    .threads = 4,
                    .name = "worker",
                    .limits = {.low = 1, .high = 20}
                };
            });
        reads<TunablesHolder>(
            kit,
            "presence_all_on_a_field_reads_what_is_there",
            [] {
                return std::map<std::string, std::variant<int, Sections>>{
                    {"count",    3                                 },
                    {"tunables", Sections{{"limits", {{"low", 0}}}}},
                };
            },
            [] {
                return TunablesHolder{
                    .tunables = {.threads = 4, .name = "worker", .limits = {.low = 0, .high = 9}},
                    .count = 3,
                };
            });
        read_fails<TunablesHolder>(kit,
                                   "presence_all_on_a_field_holder_field_missing_fails",
                                   [] {
                                       return std::map<std::string, Sections>{
                                           {"tunables", {}}
                                       };
                                   },
                                   {.message = "missing required field 'count'", .path = ""});
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
        auto with_extra = [] {
            return Ints{
                {"x",     1},
                {"y",     2},
                {"extra", 3}
            };
        };
        reads<Point>(kit, "unknown_field_ignored", with_extra, [] {
            return Point{.x = 1, .y = 2};
        });
        // An installed UnknownFields hears of every key nothing reads, under
        // the path the document gives the object holding it.
        reads_reporting<Layout>(
            kit,
            "unknown_fields_reported_at_every_depth",
            [] {
                return LayoutWithExtrasPlain{
                    .id = 1,
                    .origin = {.x = 1, .y = 2, .extra = true},
                    .points = {{.x = 3, .y = 4, .extra = true}, {.x = 5, .y = 6, .extra = true}},
                    .named = {{"a", {.x = 7, .y = 8, .extra = true}}},
                    .stray = true,
                };
            },
            [] {
                return Layout{
                    .id = 1,
                    .origin = {.x = 1, .y = 2},
                    .points = {{.x = 3, .y = 4}, {.x = 5, .y = 6}},
                    .named = {{"a", {.x = 7, .y = 8}}},
                };
            },
            {"unknown field 'stray'",
             "unknown field 'extra' at origin",
             "unknown field 'extra' at points[0]",
             "unknown field 'extra' at points[1]",
             "unknown field 'extra' at named.a"});
        reads_reporting<Point>(
            kit,
            "known_fields_report_nothing",
            [] { return Point{.x = 1, .y = 2}; },
            [] { return Point{.x = 1, .y = 2}; },
            {});
        // With detailed_error off, the unknown fields have no paths either.
        reads_reporting<Layout, NoPathConfig>(
            kit,
            "unknown_fields_reported_without_path_when_detail_is_off",
            [] {
                return LayoutWithExtrasPlain{
                    .id = 1,
                    .origin = {.x = 1, .y = 2, .extra = true},
                    .points = {{.x = 3, .y = 4, .extra = true}},
                    .named = {},
                    .stray = true,
                };
            },
            [] {
                return Layout{
                    .id = 1,
                    .origin = {.x = 1, .y = 2},
                    .points = {{.x = 3, .y = 4}},
                    .named = {},
                };
            },
            {"unknown field 'stray'", "unknown field 'extra'", "unknown field 'extra'"});
        // A decode that fails keeps what it reported before.
        read_fails_reporting<Point>(
            kit,
            "failed_decode_keeps_reported_fields_fails",
            [] { return ExtraBeforeTextPlain{.extra = true, .x = "one", .y = 2}; },
            {.message = "", .path = "x"},
            {"unknown field 'extra'"});
        // A path names a field as the document does, by an alias too.
        reads_reporting<AliasedOrigin>(
            kit,
            "unknown_field_under_alias_reported_by_alias",
            [] {
                return AnchorPlain<PointWithExtra>{
                    {.x = 1, .y = 2, .extra = true}
                };
            },
            [] {
                return AliasedOrigin{
                    {.x = 1, .y = 2}
                };
            },
            {"unknown field 'extra' at anchor"});
        read_fails<AliasedOrigin>(
            kit,
            "field_under_alias_mismatch_fails_at_alias",
            [] { return AnchorPlain<std::map<std::string, std::string>>{{{"x", "one"}}}; },
            {.message = "", .path = "anchor.x"});
        // An untagged probe that fails takes back what it reported: Measured
        // reads x and y, passes over extra, then misses its length.
        reads_reporting<Field<std::variant<Measured, Point>>>(
            kit,
            "failed_probe_takes_back_its_unknown_fields",
            [] {
                return Field<PointWithExtra>{
                    {.x = 1, .y = 2, .extra = true}
                };
            },
            [] {
                return Field<std::variant<Measured, Point>>{
                    Point{.x = 1, .y = 2}
                };
            },
            {"unknown field 'extra' at value"});
        // Where unknown fields are denied the first fails, and is not reported.
        read_fails_reporting<StrictRoot>(
            kit,
            "denied_unknown_field_fails_unreported",
            [] { return RenameTargetWithExtra{.user_name = 7, .display_name = "ada", .extra = 1}; },
            {.message = "unknown field 'extra'", .path = ""},
            {});
        read_fails<StrictRoot>(
            kit,
            "unknown_field_fails",
            [] { return RenameTargetWithExtra{.user_name = 7, .display_name = "ada", .extra = 1}; },
            {.message = "unknown field 'extra'", .path = ""});
        read_fails<Point, StrictConfig>(kit,
                                        "unknown_field_under_config_fails",
                                        with_extra,
                                        {.message = "unknown field 'extra'", .path = ""});
        read_fails<SnakeStrict>(kit,
                                "rename_all_on_field_denies_unknown_fails",
                                [] {
                                    return Field<RenameTargetUpperSnakeWithExtra>{
                                        {.USER_NAME = 7, .DISPLAY_NAME = "ada", .EXTRA = 1}
                                    };
                                },
                                {.message = "unknown field 'EXTRA'", .path = "value"});
        // In a field, so a backend that routes roots by their declared shape
        // (toml) places the variant as it places the struct.
        encodes_as(
            kit,
            "rename_all_on_untagged_variant_is_inert",
            [] {
                return Field<CamelChoice>{
                    CamelChoice{RenameTarget{.user_name = 7, .display_name = "ada"}}};
            },
            [] {
                return Field<RenameTarget>{
                    {.user_name = 7, .display_name = "ada"}
                };
            });
        reads<AnnotatedStruct>(
            kit,
            "skipped_field_ignores_its_key",
            [] { return AnnotatedWithInternal{.id = 7, .internal = "input", .value = 2.5F}; },
            [] { return AnnotatedStruct{.user_id = 7, .internal = "kept", .value = 2.5F}; });

        if constexpr(!B::caps.layout_computed) {
            read_in_field_fails_over<EnumStringConfig>(
                kit,
                "enum_repr_string_unknown_name_fails",
                [] { return std::string("nope"); },
                [] { return Access::full_control; },
                {.message = "unknown enum value 'nope'", .path = "value"});
            // nan_repr::String only encodes: the names do not read back. One
            // name, so the path does not depend on the order of the keys.
            read_in_field_fails<double, NanStringConfig>(kit,
                                                         "nan_string_read_fails",
                                                         [] { return std::string("NaN"); },
                                                         {.message = "", .path = "value"});
        }
        // A name that does not read leaves the enum as it was. `count` starts
        // as the document has it, which toml reads before `level`.
        read_fails_over(
            kit,
            "enum_string_unknown_name_fails",
            [] { return AccessGrantPlain{.level = "super_admin", .count = 1}; },
            [] { return AccessGrant{.level = Access::full_control, .count = 1}; },
            {.message = "unknown enum value 'super_admin'", .path = "level"});

        // Leaf errors are the backend's to word; their paths are the protocol's.
        using Texts = std::map<std::string, std::string>;
        auto texts_for_point = [] {
            return Texts{
                {"x", "one"},
                {"y", "two"}
            };
        };
        read_in_field_fails<Point>(kit,
                                   "nested_field_mismatch_fails",
                                   texts_for_point,
                                   {.message = "", .path = "value.x"});
        read_in_field_fails<Point, NoPathConfig>(kit,
                                                 "detailed_error_off_fails_without_path",
                                                 texts_for_point,
                                                 {.message = "", .path = ""});
        using IntOrText = std::variant<int, std::string>;
        read_in_field_fails<std::vector<int>>(kit,
                                              "element_mismatch_fails",
                                              [] { return std::vector<IntOrText>{1, "two", 3}; },
                                              {.message = "", .path = "value[1]"});
        read_in_field_fails<Ints>(kit,
                                  "map_value_mismatch_fails",
                                  [] {
                                      return std::map<std::string, IntOrText>{
                                          {"a", 1    },
                                          {"b", "two"}
                                      };
                                  },
                                  {.message = "", .path = "value.b"});
        read_in_field_fails<std::map<int, int>>(
            kit,
            "map_key_not_integer_fails",
            [] {
                return Ints{
                    {"abc", 1}
                };
            },
            {.message = "cannot parse map key 'abc' as integer", .path = "value.abc"});
        read_in_field_fails<std::map<std::uint32_t, int>>(
            kit,
            "map_key_negative_unsigned_fails",
            [] {
                return Ints{
                    {"-1", 1}
                };
            },
            {.message = "cannot parse map key '-1' as unsigned integer", .path = "value.-1"});
        auto wide_key = [] {
            return Ints{
                {"1",   1},
                {"300", 2}
            };
        };
        read_in_field_fails<std::map<std::int8_t, int>>(
            kit,
            "map_key_out_of_integer_range_fails",
            wide_key,
            {.message = "map key '300' out of integer range", .path = "value.300"});
        read_in_field_fails<std::map<std::uint8_t, int>>(
            kit,
            "map_key_out_of_unsigned_range_fails",
            wide_key,
            {.message = "map key '300' out of unsigned integer range", .path = "value.300"});
        read_in_field_fails<std::tuple<int, int>>(
            kit,
            "tuple_too_long_fails",
            [] { return std::vector<int>{1, 2, 3}; },
            {.message = "too many elements for tuple (expected 2)", .path = "value"});
        read_in_field_fails<std::tuple<int, int>>(
            kit,
            "tuple_too_short_fails",
            [] { return std::vector<int>{1}; },
            {.message = "too few elements for tuple (expected 2, got 1)", .path = "value"});
        read_in_field_fails<std::tuple<>>(
            kit,
            "empty_tuple_too_long_fails",
            [] { return std::vector<int>{1}; },
            {.message = "too many elements for tuple (expected 0)", .path = "value"});
        read_in_field_fails<std::tuple<int, int>>(
            kit,
            "tuple_element_mismatch_fails",
            [] { return std::tuple<int, std::string>{1, "two"}; },
            {.message = "", .path = "value[1]"});
    }
}

}  // namespace kotatsu::test
