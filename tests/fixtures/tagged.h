#pragma once

// Tagged-variant fixtures. The tags are written with meta's own spec API
// rather than the codec's annotation macros, so meta's tests can use them.

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "fixtures/structs.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"

namespace kota::test {

struct TaggedIntCircle {
    int radius;
};

struct TaggedIntRect {
    int width;
    int height;
};

struct ExternalTag {
    constexpr static auto spec = meta::make_struct_spec(meta::dsl::tagged = true,
                                                        meta::dsl::tag_names = {"integer", "text"});
};

struct InternalKindTag {
    constexpr static auto spec =
        meta::make_struct_spec(meta::dsl::tag = "kind", meta::dsl::tag_names = {"circle", "rect"});
};

struct AdjacentTag {
    constexpr static auto spec = meta::make_struct_spec(meta::dsl::tag = "type",
                                                        meta::dsl::content = "value",
                                                        meta::dsl::tag_names = {"integer", "text"});
};

struct TaggedTag {
    constexpr static auto spec = meta::make_struct_spec(meta::dsl::tagged = true);
};

using ExternalTagged = meta::annotate<ExternalTag>::type<std::variant<int, std::string>>;

using InternalTagged =
    meta::annotate<InternalKindTag>::type<std::variant<TaggedIntCircle, TaggedIntRect>>;

using AdjacentTagged = meta::annotate<AdjacentTag>::type<std::variant<int, std::string>>;

using TaggedRoot = meta::annotate<InternalKindTag>::type<std::variant<Circle, Rect>>;

struct TaggedFieldStruct {
    ExternalTagged ext;
    InternalTagged in;
    AdjacentTagged adj;
};

struct TaggedVariantStruct {
    meta::annotate<TaggedTag>::type<std::variant<int, std::string>> tv;
};

// One variant in each tagging, each with a monostate, a scalar and a struct
// alternative (a struct alternative only, for internal tagging), followed by
// the plain structs of the documents they encode to in a keyed backend.

struct ExternalShapeTag {
    constexpr static auto spec =
        meta::make_struct_spec(meta::dsl::tagged = true,
                               meta::dsl::tag_names = {"none", "number", "point"});
};

using ExternalShape =
    meta::annotate<ExternalShapeTag>::type<std::variant<std::monostate, int, Point>>;

struct AdjacentShapeTag {
    constexpr static auto spec =
        meta::make_struct_spec(meta::dsl::tag = "t",
                               meta::dsl::content = "c",
                               meta::dsl::tag_names = {"none", "number", "point"});
};

using AdjacentShape =
    meta::annotate<AdjacentShapeTag>::type<std::variant<std::monostate, int, Point>>;

struct InternalShapeTag {
    constexpr static auto spec =
        meta::make_struct_spec(meta::dsl::tag = "kind",
                               meta::dsl::tag_names = {"circle", "rect", "segment"});
};

using InternalShape = meta::annotate<InternalShapeTag>::type<std::variant<Circle, Rect, Segment>>;

/// The untagged variants the tagged ones become where tags do not apply.
using BareShape = std::variant<std::monostate, int, Point>;
using BareFigure = std::variant<Circle, Rect, Segment>;

struct NoneTagPlain {
    std::nullptr_t none;
};

struct PointTagPlain {
    Point point;
};

/// Two tags where an externally tagged variant takes one.
struct TwoTagsPlain {
    int number;
    Point point;
};

template <typename T>
struct AdjacentPlain {
    std::string t;
    T c;
};

template <typename T>
struct AdjacentContentFirst {
    T c;
    std::string t;
};

struct CirclePlain {
    std::string kind;
    double radius;
};

struct CircleWithExtraPlain {
    std::string kind;
    double radius;
    std::string extra;
};

struct RectPlain {
    std::string kind;
    double width;
    double height;
};

struct SegmentCamelPlain {
    std::string kind;
    int lineWidth;
};

struct TaggedHolder {
    std::string name;
    ExternalShape ext;
    AdjacentShape adj;
    InternalShape in;
};

struct TaggedHolderPlain {
    std::string name;
    PointTagPlain ext;
    AdjacentPlain<int> adj;
    CirclePlain in;
};

struct TaggedHolderBare {
    std::string name;
    BareShape ext;
    BareShape adj;
    BareFigure in;
};

struct TaggedContainers {
    std::vector<AdjacentShape> list;
    std::map<std::string, InternalShape> by_name;
    std::optional<ExternalShape> maybe;
    std::optional<ExternalShape> none;
};

/// A tagged variant inside a struct inside a tagged variant.
struct TaggedWrapper {
    std::string id;
    ExternalShape inner;
};

struct NestedTag {
    constexpr static auto spec =
        meta::make_struct_spec(meta::dsl::tagged = true,
                               meta::dsl::tag_names = {"plain", "wrapped"});
};

using NestedTagged = meta::annotate<NestedTag>::type<std::variant<int, TaggedWrapper>>;

}  // namespace kota::test
