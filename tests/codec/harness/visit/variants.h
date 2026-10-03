#pragma once

// Variants: untagged variants roundtrip, the three taggings and where tags do
// not apply, and the errors of tagged decoding. How keyed documents pick an
// untagged alternative is probing.h.

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "codec/harness/fixtures/configs.h"
#include "codec/harness/fixtures/structs.h"
#include "codec/harness/fixtures/tagged.h"
#include "codec/harness/visit/kit.h"
#include "fixtures/configs.h"
#include "fixtures/structs.h"
#include "fixtures/tagged.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"

namespace kota::test {

template <Backend B>
void variants(const Kit<B>& kit) {
    using Untagged = std::variant<std::monostate, bool, int, double, std::string, Point2d>;
    using Containers = std::variant<std::tuple<int, std::string>, std::vector<int>, Point2d>;

    roundtrip(kit, "untagged_alternatives_roundtrip", [] {
        return std::vector<Untagged>{
            true,
            42,
            3.14,
            std::string("text"),
            Point2d{.x = 1, .y = 2}
        };
    });
    if constexpr(B::caps.nested_nulls) {
        roundtrip(kit, "untagged_null_roundtrip", [] { return Field<Untagged>{std::monostate{}}; });
    }
    // Decoded over another alternative, which the decode must replace.
    roundtrip_over(
        kit,
        "untagged_over_another_alternative_roundtrip",
        [] {
            return Field<Untagged>{
                Point2d{.x = 1, .y = 2}
            };
        },
        [] { return Field<Untagged>{std::string("other")}; });
    roundtrip(kit, "untagged_containers_roundtrip", [] {
        return std::vector<Containers>{
            std::tuple<int, std::string>{7, "seven"},
            std::vector<int>{1, 2, 3},
            Point2d{.x = 1, .y = 2}
        };
    });
    roundtrip(kit, "untagged_in_optional_roundtrip", [] {
        return std::vector<std::optional<std::variant<int, std::string>>>{42, "text"};
    });

    auto holder = [] {
        return TaggedHolder{
            .name = "h",
            .ext = Point{.x = 1, .y = 2},
            .adj = 7,
            .in = Circle{.radius = 1.5}
        };
    };
    // Tags shape a keyed document; elsewhere a tagged variant travels as the
    // untagged one would. On a keyed backend the variant stands in a field,
    // as the plain struct does, so a backend that routes roots by their
    // declared shape (toml) compares the two alike.
    if constexpr(B::caps.self_describing) {
        encodes_as(
            kit,
            "external_encodes_as_plain",
            [] { return Field<ExternalShape>{ExternalShape(Point{.x = 1, .y = 2})}; },
            [] { return Field<PointTagPlain>{{.point = {.x = 1, .y = 2}}}; });
        encodes_as(
            kit,
            "external_monostate_encodes_as_plain",
            [] { return Field<ExternalShape>{ExternalShape{std::monostate{}}}; },
            [] { return Field<NoneTagPlain>{}; });
        encodes_as(
            kit,
            "adjacent_encodes_as_plain",
            [] { return Field<AdjacentShape>{AdjacentShape{7}}; },
            [] {
                return Field<AdjacentPlain<int>>{
                    {.t = "number", .c = 7}
                };
            });
        encodes_as(
            kit,
            "adjacent_monostate_encodes_as_plain",
            [] { return Field<AdjacentShape>{AdjacentShape{std::monostate{}}}; },
            [] {
                return Field<AdjacentPlain<std::nullptr_t>>{
                    {.t = "none", .c = nullptr}
                };
            });
        encodes_as(
            kit,
            "internal_encodes_as_plain",
            [] { return Field<InternalShape>{InternalShape(Rect{.width = 2, .height = 3})}; },
            [] {
                return Field<RectPlain>{
                    {.kind = "rect", .width = 2, .height = 3}
                };
            });
        encodes_as(kit, "tagged_in_struct_encodes_as_plain", holder, [] {
            return TaggedHolderPlain{
                .name = "h",
                .ext = {.point = {.x = 1, .y = 2}},
                .adj = {.t = "number", .c = 7},
                .in = {.kind = "circle", .radius = 1.5},
            };
        });
        using DefaultNamed = meta::annotate<TaggedTag>::type<std::variant<Circle, Rect>>;
        encodes_as(
            kit,
            "default_tag_names_are_type_names",
            [] { return Field<DefaultNamed>{DefaultNamed{Circle{.radius = 1.5}}}; },
            [] { return Field<std::map<std::string, Circle>>{{{"Circle", {.radius = 1.5}}}}; });
    }
    // Where a document is not human readable, each tagging travels as the
    // bare variant: a positional backend always, a keyed one under a config
    // that says so.
    auto bare = [] {
        return TaggedHolderBare{
            .name = "h",
            .ext = Point{.x = 1, .y = 2},
            .adj = 7,
            .in = Circle{.radius = 1.5}
        };
    };
    if constexpr(B::caps.self_describing) {
        encodes_as<NotHumanReadableConfig>(kit, "not_human_readable_ignores_tags", holder, bare);
    } else {
        encodes_as(kit, "tags_ignored_encodes_as_bare", holder, bare);
    }
    roundtrip(kit, "tagged_roundtrip", [] {
        TaggedHolder none{.name = "a", .ext = {}, .adj = {}, .in = Circle{.radius = 1.5}};
        TaggedHolder number{
            .name = "b",
            .ext = 7,
            .adj = 7,
            .in = Rect{.width = 2, .height = 3}
        };
        TaggedHolder text{.name = "c",
                          .ext = std::string(),
                          .adj = std::string("text"),
                          .in = Segment{.line_width = 5}};
        TaggedHolder point{
            .name = "d",
            .ext = Point{.x = 1, .y = 2},
            .adj = Point{.x = 3, .y = 4},
            .in = Circle{.radius = 0}
        };
        // The monostate alternatives carry their null in a field.
        if constexpr(B::caps.nested_nulls) {
            return std::vector<TaggedHolder>{none, number, text, point};
        } else {
            return std::vector<TaggedHolder>{number, text, point};
        }
    });
    // `none` starts engaged where nulls travel, so a null that did not reset
    // it shows.
    roundtrip_over(
        kit,
        "tagged_in_containers_roundtrip",
        [] {
            return TaggedContainers{
                .list = {AdjacentShape{1}, AdjacentShape{Point{.x = 1, .y = 2}}},
                .by_name = {{"c", Circle{.radius = 1}}, {"r", Rect{.width = 2, .height = 3}}},
                .maybe = ExternalShape{7},
                .none = std::nullopt,
            };
        },
        [] {
            TaggedContainers start;
            if constexpr(B::caps.nested_nulls) {
                start.none = ExternalShape{1};
            }
            return start;
        });
    roundtrip(kit, "tagged_nested_roundtrip", [] {
        return NestedTagged{
            TaggedWrapper{.id = "w", .inner = Point{.x = 1, .y = 2}}
        };
    });

    if constexpr(B::caps.self_describing) {
        // Tagged decoding: what a keyed document may and may not do.
        using Ints = std::map<std::string, int>;
        using Texts = std::map<std::string, std::string>;
        read_in_field_fails<ExternalShape>(
            kit,
            "external_unknown_tag_fails",
            [] {
                return Ints{
                    {"bad", 42}
                };
            },
            {.message = "unknown variant tag 'bad'", .path = "value"});
        // The value sits below its tag, which names it in a path.
        read_in_field_fails<ExternalShape>(kit,
                                           "external_content_mismatch_fails_below_its_tag",
                                           [] {
                                               return std::map<std::string, Texts>{
                                                   {"point", {{"x", "one"}}}
                                               };
                                           },
                                           {.message = "", .path = "value.point.x"});
        read_in_field_fails<ExternalShape>(
            kit,
            "external_two_tags_fails",
            [] {
                return TwoTagsPlain{
                    .number = 1,
                    .point = {.x = 1, .y = 2}
                };
            },
            {.message = "externally tagged variant: expected exactly one field", .path = "value"});
        read_in_field_fails<ExternalShape>(
            kit,
            "external_empty_object_fails",
            [] { return Empty{}; },
            {.message = "externally tagged variant: expected exactly one field", .path = "value"});
        read_in_field_fails<ExternalShape>(kit,
                                           "external_not_an_object_fails",
                                           [] { return 42; },
                                           {.message = "", .path = "value"});
        reads_in_field<AdjacentShape>(
            kit,
            "adjacent_content_before_tag_reads",
            [] {
                return AdjacentContentFirst<Point>{
                    .c = {.x = 1, .y = 2},
                    .t = "point"
                };
            },
            [] { return AdjacentShape(Point{.x = 1, .y = 2}); });
        reads_in_field<AdjacentShape>(
            kit,
            "adjacent_extra_fields_ignored",
            [] { return AdjacentWithExtraPlain{.t = "number", .extra = true, .c = 5}; },
            [] { return AdjacentShape{5}; });
        reads_reporting<Field<AdjacentShape>>(
            kit,
            "adjacent_extra_fields_reported",
            [] {
                return Field<AdjacentWithExtraPlain>{
                    {.t = "number", .extra = true, .c = 5}
                };
            },
            [] { return Field<AdjacentShape>{AdjacentShape{5}}; },
            {"unknown field 'extra' at value"});
        read_in_field_fails<AdjacentShape, StrictConfig>(
            kit,
            "adjacent_extra_field_denied_fails",
            [] { return AdjacentWithExtraPlain{.t = "number", .extra = true, .c = 5}; },
            {.message = "unknown field 'extra'", .path = "value"});
        read_in_field_fails<AdjacentShape>(
            kit,
            "adjacent_missing_tag_fails",
            [] {
                return Ints{
                    {"c", 42}
                };
            },
            {.message = "adjacently tagged variant: missing tag field", .path = "value"});
        read_in_field_fails<AdjacentShape>(
            kit,
            "adjacent_empty_object_fails",
            [] { return Empty{}; },
            {.message = "adjacently tagged variant: missing tag field", .path = "value"});
        read_in_field_fails<AdjacentShape>(
            kit,
            "adjacent_missing_content_fails",
            [] {
                return std::map<std::string, std::string>{
                    {"t", "number"}
                };
            },
            {.message = "adjacently tagged variant: missing content field", .path = "value"});
        read_in_field_fails<AdjacentShape>(
            kit,
            "adjacent_unknown_tag_fails",
            [] { return AdjacentPlain<int>{.t = "bad", .c = 42}; },
            {.message = "unknown variant tag 'bad'", .path = "value"});
        // The value sits below the content key, which names it in a path.
        read_in_field_fails<AdjacentShape>(
            kit,
            "adjacent_content_mismatch_fails_below_its_key",
            [] { return AdjacentPlain<Texts>{.t = "point", .c = {{"x", "one"}}}; },
            {.message = "", .path = "value.c.x"});
        read_in_field_fails<AdjacentShape>(
            kit,
            "adjacent_unknown_tag_after_content_fails",
            [] { return AdjacentContentFirst<int>{.c = 42, .t = "bad"}; },
            {.message = "unknown variant tag 'bad'", .path = "value"});
        // A tag that is not text fails as the backend's type error.
        read_in_field_fails<AdjacentShape>(kit,
                                           "adjacent_tag_not_a_string_after_content_fails",
                                           [] {
                                               return Ints{
                                                   {"c", 42},
                                                   {"t", 1 }
                                               };
                                           },
                                           {.message = "", .path = "value"});
        read_in_field_fails<AdjacentShape>(kit,
                                           "adjacent_not_an_object_fails",
                                           [] { return 42; },
                                           {.message = "", .path = "value"});
        read_in_field_fails<InternalShape>(
            kit,
            "internal_unknown_tag_fails",
            [] { return CirclePlain{.kind = "pentagon", .radius = 5}; },
            {.message = "unknown variant tag 'pentagon'", .path = "value"});
        read_in_field_fails<InternalShape>(
            kit,
            "internal_missing_tag_fails",
            [] {
                return std::map<std::string, double>{
                    {"radius", 5}
                };
            },
            {.message = "internally tagged variant: missing tag field", .path = "value"});
        read_in_field_fails<InternalShape>(
            kit,
            "internal_empty_object_fails",
            [] { return Empty{}; },
            {.message = "internally tagged variant: missing tag field", .path = "value"});
        read_in_field_fails<InternalShape>(kit,
                                           "internal_tag_not_a_string_fails",
                                           [] {
                                               return Ints{
                                                   {"kind",   1},
                                                   {"radius", 5}
                                               };
                                           },
                                           {.message = "", .path = "value"});
        read_in_field_fails<InternalShape>(kit,
                                           "internal_not_an_object_fails",
                                           [] { return 42; },
                                           {.message = "", .path = "value"});
        read_in_field_fails<InternalShape>(
            kit,
            "internal_missing_required_field_fails",
            [] { return CirclePlain{.kind = "rect", .radius = 5}; },
            {.message = "missing required field 'width'", .path = "value"});
        // The tag's position among the fields does not matter.
        reads_in_field<InternalShape>(
            kit,
            "internal_tag_after_fields_reads",
            [] { return RectTagLastPlain{.width = 2, .height = 3, .kind = "rect"}; },
            [] { return InternalShape(Rect{.width = 2, .height = 3}); });
        read_in_field_fails<InternalShape>(
            kit,
            "internal_unknown_tag_after_fields_fails",
            [] { return RectTagLastPlain{.width = 2, .height = 3, .kind = "pentagon"}; },
            {.message = "unknown variant tag 'pentagon'", .path = "value"});
        read_in_field_fails<InternalShape>(kit,
                                           "internal_tag_not_a_string_after_fields_fails",
                                           [] {
                                               return std::map<std::string, double>{
                                                   {"height", 3},
                                                   {"kind",   1},
                                                   {"width",  2}
                                               };
                                           },
                                           {.message = "", .path = "value"});
        reads_in_field<InternalShape>(
            kit,
            "internal_extra_field_ignored",
            [] { return CircleWithExtraPlain{.kind = "circle", .radius = 1, .extra = "x"}; },
            [] { return InternalShape(Circle{.radius = 1}); });
        reads_in_field<InternalShape, CamelConfig>(
            kit,
            "internal_alternatives_follow_field_rename",
            [] { return SegmentCamelPlain{.kind = "segment", .lineWidth = 7}; },
            [] { return InternalShape(Segment{.line_width = 7}); });
    }
}

}  // namespace kota::test
