#include <expected>
#include <string>
#include <variant>

#include "kota/zest/zest.h"
#include "kota/support/naming.h"
#include "kota/meta/annotation.h"
#include "kota/codec/visit/config.h"
#include "kota/codec/visit/dispatch.h"

namespace kota::codec {

namespace {

struct Shape {
    int sides = 0;
};

struct LegacyAlias {
    constexpr static auto spec = meta::make_spec(meta::dsl::alias = {"corners"});
};

struct Polygon {
    meta::annotate<LegacyAlias>::type<int> vertex_count;
};

struct CamelConfig {
    using field_rename = naming::rename_policy::lower_camel;
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
    ZSTATIC_EXPECT(detail::has_field_named<default_config<>, Shape>("sides"));
    ZSTATIC_EXPECT(!detail::has_field_named<default_config<>, Shape>("kind"));
    ZSTATIC_EXPECT(detail::has_field_named<default_config<>, Polygon>("corners"));
    // Under the config's renaming, not the declared name.
    ZSTATIC_EXPECT(detail::has_field_named<default_config<CamelConfig>, Polygon>("vertexCount"));
    ZSTATIC_EXPECT(!detail::has_field_named<default_config<CamelConfig>, Polygon>("vertex_count"));
}

};  // ZEST_SUITE(codec_visit_dispatch)

}  // namespace

}  // namespace kota::codec
