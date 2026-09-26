#pragma once

// Variants: untagged variants roundtrip, the three taggings and where tags do
// not apply, and the errors of tagged decoding. How keyed documents pick an
// untagged alternative is probing.h.

#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "codec/harness/fixtures/configs.h"
#include "codec/harness/fixtures/repr.h"
#include "codec/harness/visit/kit.h"
#include "fixtures/configs.h"
#include "fixtures/containers.h"
#include "fixtures/structs.h"
#include "fixtures/tagged.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"

namespace kota::test {

namespace detail {

/// Reads plain() into Field<V> and expects Field<V>{expect}: the value stands
/// in a field, so a backend routing roots differently probes the same way.
template <typename V, typename Config = void, Backend B, typename Plain, typename Expect>
void probes(const Kit<B>& kit, std::string name, Plain plain, Expect expect) {
    reads<Field<V>, Config>(
        kit,
        std::move(name),
        [plain] { return Field<decltype(plain())>{plain()}; },
        [expect] { return Field<V>{expect()}; });
}

template <typename V, Backend B, typename Plain>
void probe_fails(const Kit<B>& kit, std::string name, Plain plain, Failure failure) {
    read_fails<Field<V>>(
        kit,
        std::move(name),
        [plain] { return Field<decltype(plain())>{plain()}; },
        failure);
}

}  // namespace detail

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
    roundtrip(kit, "untagged_null_roundtrip", [] { return Field<Untagged>{std::monostate{}}; });
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

    // Tags shape a keyed document; elsewhere a tagged variant travels as the
    // untagged one would.
    if constexpr(B::caps.self_describing) {
        encodes_as(
            kit,
            "external_encodes_as_plain",
            [] { return ExternalShape(Point{.x = 1, .y = 2}); },
            [] {
                return PointTagPlain{
                    .point = {.x = 1, .y = 2}
                };
            });
        encodes_as(
            kit,
            "external_monostate_encodes_as_plain",
            [] { return ExternalShape{std::monostate{}}; },
            [] { return NoneTagPlain{}; });
        encodes_as(
            kit,
            "adjacent_encodes_as_plain",
            [] { return AdjacentShape{7}; },
            [] { return AdjacentPlain<int>{.t = "number", .c = 7}; });
        encodes_as(
            kit,
            "adjacent_monostate_encodes_as_plain",
            [] { return AdjacentShape{std::monostate{}}; },
            [] { return AdjacentPlain<std::nullptr_t>{.t = "none", .c = nullptr}; });
        encodes_as(
            kit,
            "internal_encodes_as_plain",
            [] { return InternalShape(Rect{.width = 2, .height = 3}); },
            [] { return RectPlain{.kind = "rect", .width = 2, .height = 3}; });
        encodes_as(
            kit,
            "tagged_in_struct_encodes_as_plain",
            [] {
                return TaggedHolder{
                    .name = "h",
                    .ext = Point{.x = 1, .y = 2},
                    .adj = 7,
                    .in = Circle{.radius = 1.5}
                };
            },
            [] {
                return TaggedHolderPlain{
                    .name = "h",
                    .ext = {.point = {.x = 1, .y = 2}},
                    .adj = {.t = "number", .c = 7},
                    .in = {.kind = "circle", .radius = 1.5},
                };
            });
        encodes_as(
            kit,
            "default_tag_names_are_type_names",
            [] {
                return meta::annotate<TaggedTag>::type<std::variant<Circle, Rect>>{
                    Circle{.radius = 1.5}};
            },
            [] {
                return std::map<std::string, Circle>{
                    {"Circle", {.radius = 1.5}}
                };
            });
    } else {
        encodes_as(
            kit,
            "external_encodes_as_plain",
            [] { return ExternalShape(Point{.x = 1, .y = 2}); },
            [] { return BareShape(Point{.x = 1, .y = 2}); });
        encodes_as(
            kit,
            "adjacent_encodes_as_plain",
            [] { return AdjacentShape{7}; },
            [] { return BareShape{7}; });
        encodes_as(
            kit,
            "internal_encodes_as_plain",
            [] { return InternalShape(Rect{.width = 2, .height = 3}); },
            [] { return BareFigure(Rect{.width = 2, .height = 3}); });
    }
    encodes_as<NotHumanReadableConfig>(
        kit,
        "not_human_readable_ignores_tags",
        [] {
            return TaggedHolder{
                .name = "h",
                .ext = Point{.x = 1, .y = 2},
                .adj = 7,
                .in = Circle{.radius = 1.5}
            };
        },
        [] {
            return TaggedHolderBare{
                .name = "h",
                .ext = Point{.x = 1, .y = 2},
                .adj = 7,
                .in = Circle{.radius = 1.5}
            };
        });
    roundtrip(kit, "tagged_roundtrip", [] {
        TaggedHolder none{.name = "a", .ext = {}, .adj = {}, .in = Circle{.radius = 1.5}};
        TaggedHolder number{
            .name = "b",
            .ext = 7,
            .adj = 7,
            .in = Rect{.width = 2, .height = 3}
        };
        TaggedHolder point{
            .name = "c",
            .ext = Point{.x = 1, .y = 2},
            .adj = Point{.x = 3, .y = 4},
            .in = Segment{.line_width = 5}
        };
        return std::vector<TaggedHolder>{none, number, point};
    });
    roundtrip(kit, "tagged_in_containers_roundtrip", [] {
        return TaggedContainers{
            .list = {AdjacentShape{1}, AdjacentShape{Point{.x = 1, .y = 2}}},
            .by_name = {{"c", Circle{.radius = 1}}, {"r", Rect{.width = 2, .height = 3}}},
            .maybe = ExternalShape{7},
            .none = std::nullopt,
        };
    });
    roundtrip(kit, "tagged_nested_roundtrip", [] {
        return NestedTagged{
            TaggedWrapper{.id = "w", .inner = Point{.x = 1, .y = 2}}
        };
    });

    if constexpr(B::caps.self_describing) {
        using detail::probe_fails;
        using detail::probes;

        // Tagged decoding: what a keyed document may and may not do.
        using Ints = std::map<std::string, int>;
        probe_fails<ExternalShape>(kit,
                                   "external_unknown_tag_fails",
                                   [] {
                                       return Ints{
                                           {"bad", 42}
                                       };
                                   },
                                   {.message = "unknown variant tag 'bad'", .path = "value"});
        probe_fails<ExternalShape>(
            kit,
            "external_two_tags_fails",
            [] {
                return TwoTagsPlain{
                    .number = 1,
                    .point = {.x = 1, .y = 2}
                };
            },
            {.message = "externally tagged variant: expected exactly one field", .path = "value"});
        probe_fails<ExternalShape>(
            kit,
            "external_empty_object_fails",
            [] { return Empty{}; },
            {.message = "externally tagged variant: expected exactly one field", .path = "value"});
        probe_fails<ExternalShape>(kit,
                                   "external_not_an_object_fails",
                                   [] { return 42; },
                                   {.message = "", .path = "value"});
        probes<AdjacentShape>(
            kit,
            "adjacent_content_before_tag_reads",
            [] {
                return AdjacentContentFirst<Point>{
                    .c = {.x = 1, .y = 2},
                    .t = "point"
                };
            },
            [] {
                return AdjacentShape{
                    Point{.x = 1, .y = 2}
                };
            });
        probe_fails<AdjacentShape>(
            kit,
            "adjacent_missing_tag_fails",
            [] {
                return Ints{
                    {"c", 42}
                };
            },
            {.message = "adjacently tagged variant: missing tag field", .path = "value"});
        probe_fails<AdjacentShape>(
            kit,
            "adjacent_missing_content_fails",
            [] {
                return std::map<std::string, std::string>{
                    {"t", "number"}
                };
            },
            {.message = "adjacently tagged variant: missing content field", .path = "value"});
        probe_fails<AdjacentShape>(kit,
                                   "adjacent_unknown_tag_fails",
                                   [] { return AdjacentPlain<int>{.t = "bad", .c = 42}; },
                                   {.message = "unknown variant tag 'bad'", .path = "value"});
        probe_fails<AdjacentShape>(kit,
                                   "adjacent_unknown_tag_after_content_fails",
                                   [] { return AdjacentContentFirst<int>{.c = 42, .t = "bad"}; },
                                   {.message = "unknown variant tag 'bad'", .path = "value"});
        probe_fails<AdjacentShape>(kit,
                                   "adjacent_not_an_object_fails",
                                   [] { return 42; },
                                   {.message = "", .path = "value"});
        probe_fails<InternalShape>(kit,
                                   "internal_unknown_tag_fails",
                                   [] { return CirclePlain{.kind = "pentagon", .radius = 5}; },
                                   {.message = "unknown variant tag 'pentagon'", .path = "value"});
        probe_fails<InternalShape>(
            kit,
            "internal_missing_tag_fails",
            [] {
                return std::map<std::string, double>{
                    {"radius", 5}
                };
            },
            {.message = "internally tagged variant: missing tag field", .path = "value"});
        probe_fails<InternalShape>(
            kit,
            "internal_empty_object_fails",
            [] { return Empty{}; },
            {.message = "internally tagged variant: missing tag field", .path = "value"});
        probe_fails<InternalShape>(kit,
                                   "internal_tag_not_a_string_fails",
                                   [] {
                                       return Ints{
                                           {"kind",   1},
                                           {"radius", 5}
                                       };
                                   },
                                   {.message = "", .path = "value"});
        probe_fails<InternalShape>(kit,
                                   "internal_not_an_object_fails",
                                   [] { return 42; },
                                   {.message = "", .path = "value"});
        probe_fails<InternalShape>(kit,
                                   "internal_missing_required_field_fails",
                                   [] { return CirclePlain{.kind = "rect", .radius = 5}; },
                                   {.message = "missing required field 'width'", .path = "value"});
        probes<InternalShape>(
            kit,
            "internal_extra_field_ignored",
            [] { return CircleWithExtraPlain{.kind = "circle", .radius = 1, .extra = "x"}; },
            [] { return InternalShape{Circle{.radius = 1}}; });
        probes<InternalShape, CamelConfig>(
            kit,
            "internal_alternatives_follow_field_rename",
            [] { return SegmentCamelPlain{.kind = "segment", .lineWidth = 7}; },
            [] { return InternalShape{Segment{.line_width = 7}}; });
    }
}

}  // namespace kota::test
