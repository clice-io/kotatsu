#include "codec/harness/visit/variants.h"
#include "codec/json/harness/backend.h"

namespace kota::codec {

namespace {

ZEST_SUITE(codec_json_visit_variants) {

ZEST_CASE_GROUP(protocol) {
    test::variants(test::Kit<test::Json>{add_case});
}

};  // ZEST_SUITE(codec_json_visit_variants)

}  // namespace

}  // namespace kota::codec
