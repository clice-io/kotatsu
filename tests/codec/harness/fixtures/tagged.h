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
#include "kotatsu/meta/annotation.h"
#include "kotatsu/meta/attrs.h"
#include "kotatsu/codec/macro.h"

namespace kotatsu::test {

// One variant in each tagging, each with a monostate, a number, a text and a
// struct alternative (struct alternatives only, for internal tagging),
// followed by the plain structs of the documents they encode to in a keyed
// backend.

KANNOTATE_TYPE(struct ExternalShape,
               tagged = true,
               tag_names = {"none", "number", "text", "point"});

struct ExternalShape : std::variant<std::monostate, int, std::string, Point> {
    using variant::variant;
};

KANNOTATE_TYPE(struct AdjacentShape,
               tag = "t",
               content = "c",
               tag_names = {"none", "number", "text", "point"});

struct AdjacentShape : std::variant<std::monostate, int, std::string, Point> {
    using variant::variant;
};

KANNOTATE_TYPE(struct InternalShape, tag = "kind", tag_names = {"circle", "rect", "segment"});

struct InternalShape : std::variant<Circle, Rect, Segment> {
    using variant::variant;
};

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

KANNOTATE_TYPE(struct NestedTagged, tagged = true, tag_names = {"plain", "wrapped"});

struct NestedTagged : std::variant<int, TaggedWrapper> {
    using variant::variant;
};

/// Externally tagged without tag names: the alternatives' type names tag them.
struct DefaultNamed {
    KANNOTATE(value, tagged = true)
    std::variant<Circle, Rect> value;
};

}  // namespace kotatsu::test
