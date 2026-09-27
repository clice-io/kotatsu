#include "codec/bincode/harness/backend.h"
#include "codec/harness/fixtures/everything.h"
#include "kota/zest/zest.h"
#include "kota/codec/bincode/bincode.h"

namespace kota::codec {

namespace {

ZEST_SUITE(codec_bincode_encode) {

ZEST_CASE(everything_lowering) {
    // How each kind lowers into bincode's bytes, in one document: the byte
    // layout is the format.
    auto document = bincode::to_bytes(test::Everything::typical());
    ASSERT(document);
    EXPECT_SNAPSHOT(test::Bincode::render(*document));
}

};  // ZEST_SUITE(codec_bincode_encode)

}  // namespace

}  // namespace kota::codec
