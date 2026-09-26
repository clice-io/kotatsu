#include "codec/harness/visit/attrs.h"
#include "codec/json/harness/backend.h"

namespace kota::codec {

namespace {

ZEST_SUITE(codec_json_visit_attrs) {

ZEST_CASE_GROUP(protocol) {
    test::attrs(test::Kit<test::Json>{add_case});
}

};  // ZEST_SUITE(codec_json_visit_attrs)

}  // namespace

}  // namespace kota::codec
