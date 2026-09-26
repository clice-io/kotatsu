#include "codec/fbs/harness/backend.h"
#include "codec/harness/visit/values.h"

namespace kota::codec {

namespace {

ZEST_SUITE(codec_fbs_visit_values) {

ZEST_CASE_GROUP(protocol) {
    test::values(test::Kit<test::Fbs>{add_case});
}

};  // ZEST_SUITE(codec_fbs_visit_values)

}  // namespace

}  // namespace kota::codec
