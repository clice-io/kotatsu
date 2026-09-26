#include "codec/harness/visit/variants.h"
#include "codec/toml/harness/backend.h"

namespace kota::codec {

namespace {

ZEST_SUITE(codec_toml_visit_variants) {

ZEST_CASE_GROUP(protocol) {
    test::variants(test::Kit<test::Toml>{add_case});
}

};  // ZEST_SUITE(codec_toml_visit_variants)

}  // namespace

}  // namespace kota::codec
