#include "codec/bincode/harness/backend.h"
#include "codec/harness/visit/attrs.h"

namespace kota::codec {

namespace {

ZEST_SUITE(codec_bincode_visit_attrs) {

ZEST_CASE_GROUP(protocol) {
    test::attrs(test::Kit<test::Bincode>{add_case});
}

};  // ZEST_SUITE(codec_bincode_visit_attrs)

}  // namespace

}  // namespace kota::codec
