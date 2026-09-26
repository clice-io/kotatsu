#include "codec/harness/visit/probing.h"
#include "codec/toml/harness/backend.h"

namespace kota::codec {

namespace {

ZEST_SUITE(codec_toml_visit_probing) {

ZEST_CASE_GROUP(protocol) {
    test::probing(test::Kit<test::Toml>{add_case});
}

};  // ZEST_SUITE(codec_toml_visit_probing)

}  // namespace

}  // namespace kota::codec
