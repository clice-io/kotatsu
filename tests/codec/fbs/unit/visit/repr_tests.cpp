#include "codec/fbs/harness/backend.h"
#include "codec/harness/visit/repr.h"

namespace kotatsu::codec {

namespace {

ZEST_SUITE(codec_fbs_visit_repr) {

ZEST_CASE_GROUP(protocol) {
    test::repr(test::Kit<test::Fbs>{add_case});
}

};  // ZEST_SUITE(codec_fbs_visit_repr)

}  // namespace

}  // namespace kotatsu::codec
