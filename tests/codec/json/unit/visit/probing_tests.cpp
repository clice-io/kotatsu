#include "codec/harness/visit/probing.h"
#include "codec/json/harness/backend.h"

namespace kota::codec {

namespace {

ZEST_SUITE(codec_json_visit_probing) {

ZEST_CASE_GROUP(protocol) {
    test::probing(test::Kit<test::Json>{add_case});
}

};  // ZEST_SUITE(codec_json_visit_probing)

}  // namespace

}  // namespace kota::codec
