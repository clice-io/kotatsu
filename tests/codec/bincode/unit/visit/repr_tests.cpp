#include "codec/bincode/harness/backend.h"
#include "codec/harness/visit/repr.h"

namespace kota::codec {

namespace {

ZEST_SUITE(codec_bincode_visit_repr) {

ZEST_CASE_GROUP(protocol) {
    test::repr(test::Kit<test::Bincode>{add_case});
}

};  // ZEST_SUITE(codec_bincode_visit_repr)

}  // namespace

}  // namespace kota::codec
