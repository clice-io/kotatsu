#include <expected>
#include <string>
#include <variant>

#include "kota/zest/zest.h"
#include "kota/codec/visit/dispatch.h"

namespace kota::codec {

namespace {

struct Shape {
    int sides = 0;
};

ZEST_SUITE(codec_visit_dispatch) {

ZEST_CASE(only_a_variant_takes_a_tagging_spec) {
    STATIC_EXPECT(detail::taggable<std::variant<int, std::string>>);
    STATIC_EXPECT(!detail::taggable<Shape>);
    STATIC_EXPECT(!detail::taggable<int>);
    // An expected is variant-kinded, but has no alternatives to tag.
    STATIC_EXPECT(!detail::taggable<std::expected<int, std::string>>);
}

};  // ZEST_SUITE(codec_visit_dispatch)

}  // namespace

}  // namespace kota::codec
