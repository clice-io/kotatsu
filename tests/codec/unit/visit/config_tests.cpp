#include "kota/zest/zest.h"
#include "kota/codec/visit/config.h"

namespace kota::codec {

namespace {

/// Visitors as the dispatch sees them: only their human_readable trait.
struct TextVisitor {
    constexpr static bool human_readable = true;
};

struct BinaryVisitor {
    constexpr static bool human_readable = false;
};

struct UndeclaredVisitor {};

struct TagsOn {
    constexpr static bool human_readable = true;
};

struct TagsOff {
    constexpr static bool human_readable = false;
};

ZEST_SUITE(codec_visit_config) {

ZEST_CASE(human_readable_config_fits_text_visitors) {
    STATIC_EXPECT(detail::human_readable_allowed<default_config<>, TextVisitor>);
    STATIC_EXPECT(detail::human_readable_allowed<default_config<TagsOn>, TextVisitor>);
    STATIC_EXPECT(detail::human_readable_allowed<default_config<TagsOff>, TextVisitor>);
    STATIC_EXPECT(detail::human_readable_allowed<default_config<TagsOn>, UndeclaredVisitor>);
}

ZEST_CASE(human_readable_config_turns_binary_visitors_off_only) {
    STATIC_EXPECT(detail::human_readable_allowed<default_config<>, BinaryVisitor>);
    STATIC_EXPECT(detail::human_readable_allowed<default_config<TagsOff>, BinaryVisitor>);
    STATIC_EXPECT(!detail::human_readable_allowed<default_config<TagsOn>, BinaryVisitor>);
}

};  // ZEST_SUITE(codec_visit_config)

}  // namespace

}  // namespace kota::codec
