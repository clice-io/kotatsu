#include "codec/fbs/harness/backend.h"
#include "codec/harness/visit/variants.h"

namespace kotatsu::codec {

namespace {

ZEST_SUITE(codec_fbs_visit_variants) {

ZEST_CASE_GROUP(protocol) {
    test::variants(test::Kit<test::Fbs>{add_case});
}

};  // ZEST_SUITE(codec_fbs_visit_variants)

}  // namespace

}  // namespace kotatsu::codec
