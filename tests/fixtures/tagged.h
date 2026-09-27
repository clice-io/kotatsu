#pragma once

// Tagged-variant fixtures. The tags are written with meta's own spec API
// rather than the codec's annotation macros, so meta's tests can use them.

#include <string>
#include <variant>

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

}  // namespace kota::test
