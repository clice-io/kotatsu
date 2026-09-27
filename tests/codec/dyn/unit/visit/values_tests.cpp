#include "codec/dyn/harness/backend.h"
#include "codec/harness/visit/values.h"

namespace kota::codec {

namespace {

ZEST_SUITE(codec_dyn_visit_values) {

ZEST_CASE_GROUP(protocol) {
    test::values(test::Kit<test::Dyn>{add_case});
}

};  // ZEST_SUITE(codec_dyn_visit_values)

}  // namespace

}  // namespace kota::codec
