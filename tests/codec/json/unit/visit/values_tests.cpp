#include "codec/harness/visit/values.h"
#include "codec/json/harness/backend.h"

namespace kota::codec {

namespace {

ZEST_SUITE(codec_json_visit_values) {

ZEST_CASE_GROUP(protocol) {
    test::values(test::Kit<test::Json>{add_case});
}

};  // ZEST_SUITE(codec_json_visit_values)

}  // namespace

}  // namespace kota::codec
