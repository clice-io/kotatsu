#include "codec/dyn/harness/backend.h"
#include "codec/harness/visit/probing.h"

namespace kota::codec {

namespace {

ZEST_SUITE(codec_dyn_visit_probing) {

ZEST_CASE_GROUP(protocol) {
    test::probing(test::Kit<test::Dyn>{add_case});
}

};  // ZEST_SUITE(codec_dyn_visit_probing)

}  // namespace

}  // namespace kota::codec
