#include "codec/harness/visit/attrs.h"
#include "codec/toml/harness/backend.h"

namespace kota::codec {

namespace {

ZEST_SUITE(codec_toml_visit_attrs) {

ZEST_CASE_GROUP(protocol) {
    test::attrs(test::Kit<test::Toml>{add_case});
}

};  // ZEST_SUITE(codec_toml_visit_attrs)

}  // namespace

}  // namespace kota::codec
