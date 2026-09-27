#include "codec/harness/visit/repr.h"
#include "codec/toml/harness/backend.h"

namespace kota::codec {

namespace {

ZEST_SUITE(codec_toml_visit_repr) {

ZEST_CASE_GROUP(protocol) {
    test::repr(test::Kit<test::Toml>{add_case});
}

};  // ZEST_SUITE(codec_toml_visit_repr)

}  // namespace

}  // namespace kota::codec
