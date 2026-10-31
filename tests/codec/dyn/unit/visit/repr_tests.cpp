#include "codec/dyn/harness/backend.h"
#include "codec/harness/visit/repr.h"

namespace kotatsu::codec {

namespace {

ZEST_SUITE(codec_dyn_visit_repr) {

ZEST_CASE_GROUP(protocol) {
    test::repr(test::Kit<test::Dyn>{add_case});
}

};  // ZEST_SUITE(codec_dyn_visit_repr)

}  // namespace

}  // namespace kotatsu::codec
