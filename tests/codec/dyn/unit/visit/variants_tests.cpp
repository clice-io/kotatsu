#include "codec/dyn/harness/backend.h"
#include "codec/harness/visit/variants.h"

namespace kota::codec {

namespace {

ZEST_SUITE(codec_dyn_visit_variants) {

ZEST_CASE_GROUP(protocol) {
    test::variants(test::Kit<test::Dyn>{add_case});
}

};  // ZEST_SUITE(codec_dyn_visit_variants)

}  // namespace

}  // namespace kota::codec
