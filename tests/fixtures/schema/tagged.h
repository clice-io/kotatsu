#pragma once

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

struct TaggedIntCircle {
    int radius;
};

struct TaggedIntRect {
    int width;
    int height;
};

using ExternalTagged =
    annotation<std::variant<int, std::string>, attrs::externally_tagged::names<"integer", "text">>;

using InternalTagged = annotation<std::variant<TaggedIntCircle, TaggedIntRect>,
                                  attrs::internally_tagged<"kind">::names<"circle", "rect">>;

using AdjacentTagged =
    annotation<std::variant<int, std::string>,
               attrs::adjacently_tagged<"type", "value">::names<"integer", "text">>;

using TaggedRoot = annotation<std::variant<Circle, Rect>,
                              attrs::internally_tagged<"kind">::names<"circle", "rect">>;

struct TaggedFieldStruct {
    ExternalTagged ext;
    InternalTagged in;
    AdjacentTagged adj;
};

struct TaggedVariantStruct {
    annotation<std::variant<int, std::string>, attrs::tagged<>> tv;
};

}  // namespace kota::meta::fixtures
