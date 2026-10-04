#include <cstdint>
#include <memory>
#include <vector>

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
    ZASSERT(document);
    ZEXPECT(zest::snapshot(test::Bincode::render(*document)));
}

ZEST_CASE(weak_ptr_writes_like_shared_ptr) {
    // A weak pointer writes what the shared pointer it locks to writes,
    // presence byte included, live or expired.
    auto owner = std::make_shared<std::int32_t>(7);
    std::weak_ptr<std::int32_t> live = owner;
    std::weak_ptr<std::int32_t> expired = std::make_shared<std::int32_t>(1);

    auto live_bytes = bincode::to_bytes(live);
    auto owner_bytes = bincode::to_bytes(owner);
    ZASSERT(live_bytes);
    ZASSERT(owner_bytes);
    ZEXPECT(*live_bytes == *owner_bytes);
    ZASSERT(!live_bytes->empty());
    ZEXPECT(live_bytes->front() == std::byte{0x01});

    auto expired_bytes = bincode::to_bytes(expired);
    ZASSERT(expired_bytes);
    ZEXPECT(*expired_bytes == std::vector<std::byte>{std::byte{0x00}});
}

};  // ZEST_SUITE(codec_bincode_encode)

}  // namespace

}  // namespace kota::codec
