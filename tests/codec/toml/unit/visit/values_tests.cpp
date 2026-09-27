#include "codec/harness/visit/values.h"
#include "codec/toml/harness/backend.h"

namespace kota::codec {

namespace {

ZEST_SUITE(codec_toml_visit_values) {

ZEST_CASE_GROUP(protocol) {
    test::values(test::Kit<test::Toml>{add_case});
}

};  // ZEST_SUITE(codec_toml_visit_values)

}  // namespace

}  // namespace kota::codec
