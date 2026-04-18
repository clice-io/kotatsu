#pragma once

// Tagged-variant fixtures — external / internal / adjacent tagging.

#include <string>
#include <variant>

#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"

namespace kota::meta::fixtures {

struct Circle {
    double radius;
};

struct Rect {
    double width;
    double height;
};

struct TaggedCircle {
    int radius;
};

struct TaggedRect {
    int width;
    int height;
};

using ExternalTagged =
    annotation<std::variant<int, std::string>, attrs::externally_tagged::names<"integer", "text">>;

using InternalTagged = annotation<std::variant<TaggedCircle, TaggedRect>,
                                  attrs::internally_tagged<"kind">::names<"circle", "rect">>;

using AdjacentTagged =
    annotation<std::variant<int, std::string>,
               attrs::adjacently_tagged<"type", "value">::names<"integer", "text">>;

using TaggedRoot = annotation<std::variant<Circle, Rect>,
                              attrs::internally_tagged<"kind">::names<"circle", "rect">>;

using ExternalTaggedDefault = annotation<std::variant<int, std::string>, attrs::externally_tagged>;
using InternalTaggedDefault =
    annotation<std::variant<TaggedCircle, TaggedRect>, attrs::internally_tagged<"kind">>;
using AdjacentTaggedDefault =
    annotation<std::variant<int, std::string>, attrs::adjacently_tagged<"type", "value">>;

using SingleAltTagged = annotation<std::variant<int>, attrs::externally_tagged>;

using NestedTagged = annotation<std::variant<std::variant<int, std::string>, bool>,
                                attrs::externally_tagged::names<"inner", "flag">>;

struct TaggedFieldStruct {
    ExternalTagged ext;
    InternalTagged in;
    AdjacentTagged adj;
};

struct TaggedVariantStruct {
    annotation<std::variant<int, std::string>, attrs::tagged<>> tv;
};

}  // namespace kota::meta::fixtures
