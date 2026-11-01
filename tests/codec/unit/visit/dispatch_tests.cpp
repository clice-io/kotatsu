#include <expected>
#include <string>
#include <variant>

#include "kotatsu/zest/zest.h"
#include "kotatsu/support/naming.h"
#include "kotatsu/meta/annotation.h"
#include "kotatsu/codec/format.h"
#include "kotatsu/codec/macro.h"
#include "kotatsu/codec/type_info.h"

namespace kotatsu::codec {

namespace {

struct Shape {
    int sides = 0;
};

struct Polygon {
    KANNOTATE(&vertex_count, alias = {"corners"})
    int vertex_count;
};

struct CamelSettings {
    using rename_all = naming::rename_policy::lower_camel;
};

ZEST_SUITE(codec_visit_dispatch) {

ZEST_CASE(only_a_variant_takes_a_tagging_spec) {
    ZSTATIC_EXPECT(detail::taggable<std::variant<int, std::string>>);
    ZSTATIC_EXPECT(!detail::taggable<Shape>);
    ZSTATIC_EXPECT(!detail::taggable<int>);
    // An expected is variant-kinded, but has no alternatives to tag.
    ZSTATIC_EXPECT(!detail::taggable<std::expected<int, std::string>>);
}

ZEST_CASE(field_named_by_name_alias_or_rename) {
    ZSTATIC_EXPECT(detail::has_field_named<codec::format, Shape>("sides"));
    ZSTATIC_EXPECT(!detail::has_field_named<codec::format, Shape>("kind"));
    ZSTATIC_EXPECT(detail::has_field_named<codec::format, Polygon>("corners"));
    // Under the format's renaming, not the declared name.
    ZSTATIC_EXPECT(detail::has_field_named<CamelSettings, Polygon>("vertexCount"));
    ZSTATIC_EXPECT(!detail::has_field_named<CamelSettings, Polygon>("vertex_count"));
}

};  // ZEST_SUITE(codec_visit_dispatch)

}  // namespace

}  // namespace kotatsu::codec
