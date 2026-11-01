#pragma once

// Tagged-variant fixtures. The tags are written as KANNOTATE and
// KANNOTATE_TYPE (in codec's macro sheet) expand to, with meta's own spec
// API, so meta's tests can use them.

#include <string>
#include <type_traits>
#include <variant>

#include "fixtures/structs.h"
#include "kotatsu/meta/annotation.h"
#include "kotatsu/codec/attribute.h"

namespace kotatsu::test {

struct TaggedIntCircle {
    int radius;
};

struct TaggedIntRect {
    int width;
    int height;
};

[[maybe_unused]] constexpr inline auto InternalKindTag =
    meta::make_annotation(codec::dsl::tag = "kind", codec::dsl::tag_names = {"circle", "rect"});

struct ExternalTagged;

constexpr auto __kotatsu__type_annotation(std::type_identity<ExternalTagged>) {
    return meta::make_annotation(codec::dsl::tagged = true,
                                 codec::dsl::tag_names = {"integer", "text"});
}

struct ExternalTagged : std::variant<int, std::string> {
    using variant::variant;
};

struct InternalTagged;

constexpr auto __kotatsu__type_annotation(std::type_identity<InternalTagged>) {
    return InternalKindTag;
}

struct InternalTagged : std::variant<TaggedIntCircle, TaggedIntRect> {
    using variant::variant;
};

struct AdjacentTagged;

constexpr auto __kotatsu__type_annotation(std::type_identity<AdjacentTagged>) {
    return meta::make_annotation(codec::dsl::tag = "type",
                                 codec::dsl::content = "value",
                                 codec::dsl::tag_names = {"integer", "text"});
}

struct AdjacentTagged : std::variant<int, std::string> {
    using variant::variant;
};

struct TaggedRoot;

constexpr auto __kotatsu__type_annotation(std::type_identity<TaggedRoot>) {
    return InternalKindTag;
}

struct TaggedRoot : std::variant<Circle, Rect> {
    using variant::variant;
};

struct TaggedFieldStruct {
    ExternalTagged ext;
    InternalTagged in;
    AdjacentTagged adj;
};

struct TaggedVariantStruct {
    static consteval auto __kotatsu__annotation(meta::name_key<"tv">) {
        return meta::make_annotation(codec::dsl::tagged = true);
    }

    std::variant<int, std::string> tv;
};

}  // namespace kotatsu::test
