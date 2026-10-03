#pragma once

// Tagged-variant fixtures only the codec's tests use.

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "codec/harness/fixtures/structs.h"
#include "fixtures/structs.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"

namespace kota::test {

// One variant in each tagging, each with a monostate, a number, a text and a
// struct alternative (struct alternatives only, for internal tagging),
// followed by the plain structs of the documents they encode to in a keyed
// backend.

struct ExternalShapeTag {
    constexpr static auto spec =
        meta::make_struct_spec(meta::dsl::tagged = true,
                               meta::dsl::tag_names = {"none", "number", "text", "point"});
};

using ExternalShape =
    meta::annotate<ExternalShapeTag>::type<std::variant<std::monostate, int, std::string, Point>>;

struct AdjacentShapeTag {
    constexpr static auto spec =
        meta::make_struct_spec(meta::dsl::tag = "t",
                               meta::dsl::content = "c",
                               meta::dsl::tag_names = {"none", "number", "text", "point"});
};

using AdjacentShape =
    meta::annotate<AdjacentShapeTag>::type<std::variant<std::monostate, int, std::string, Point>>;

struct InternalShapeTag {
    constexpr static auto spec =
        meta::make_struct_spec(meta::dsl::tag = "kind",
                               meta::dsl::tag_names = {"circle", "rect", "segment"});
};

using InternalShape = meta::annotate<InternalShapeTag>::type<std::variant<Circle, Rect, Segment>>;

/// The untagged variants the tagged ones become where tags do not apply.
using BareShape = std::variant<std::monostate, int, std::string, Point>;
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

struct AdjacentWithExtraPlain {
    std::string t;
    bool extra;
    int c;
};

/// The point of an externally tagged point with a key Point does not have.
struct PointWithExtraTagPlain {
    PointWithExtra point;
};

/// An adjacently tagged point with a key beside the tag and the content, and
/// a key Point does not have in the content.
struct AdjacentPointWithExtrasPlain {
    std::string t;
    bool extra;
    PointWithExtra c;
};

struct CirclePlain {
    std::string kind;
    double radius;
};

/// The tag after a data field in every backend's document: toml writes
/// keys in order, and "height" comes before "kind".
struct RectTagLastPlain {
    double width;
    double height;
    std::string kind;
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

/// `ext` and `adj` start on a number, so a monostate that did not reach
/// them shows.
struct TaggedHolder {
    std::string name;
    ExternalShape ext = ExternalShape{1};
    AdjacentShape adj = AdjacentShape{1};
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
