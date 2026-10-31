#include "codec/harness/visit/repr.h"
#include "codec/json/harness/backend.h"

namespace kotatsu::codec {

namespace {

ZEST_SUITE(codec_json_visit_repr) {

ZEST_CASE_GROUP(protocol) {
    test::repr(test::Kit<test::Json>{add_case});
}

};  // ZEST_SUITE(codec_json_visit_repr)

}  // namespace

}  // namespace kotatsu::codec
