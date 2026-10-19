#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "codec/blob/harness/fixtures.h"
#include "kota/zest/zest.h"
#include "kota/codec/blob/blob.h"

namespace kota::codec::blob {

namespace {

std::uint32_t u32_at(std::span<const std::byte> bytes, std::size_t at) {
    std::uint32_t value = 0;
    std::memcpy(&value, bytes.data() + at, sizeof(value));
    return value;
}

ZEST_SUITE(codec_blob_encode) {

ZEST_CASE(probe_bytes_follow_the_layout) {
    auto bytes = test::blob_of(test::probe());
    ZASSERT(bytes.size() == 40U);
    ZEXPECT(u32_at(bytes, 0) == 0x424f4c42U);  // "BLOB"
    ZEXPECT(u32_at(bytes, 4) == detail::fingerprint<view_t<test::Probe>>());
    ZEXPECT(bytes[8] == std::byte{1});
    // Each Vec counts from its own address: the string's from 12 to 28, the
    // sequence's from 20 to 32, the next multiple of 8 past the string.
    ZEXPECT(u32_at(bytes, 12) == 16U);
    ZEXPECT(u32_at(bytes, 16) == 3U);
    ZEXPECT(u32_at(bytes, 20) == 12U);
    ZEXPECT(u32_at(bytes, 24) == 2U);
    ZEXPECT(bytes[28] == std::byte{'a'});
    ZEXPECT(bytes[29] == std::byte{'b'});
    ZEXPECT(bytes[30] == std::byte{'c'});
    ZEXPECT(u32_at(bytes, 32) == 1U);
    ZEXPECT(u32_at(bytes, 36) == 2U);
    // Padding: after the bool, and between the string and the sequence.
    for(std::size_t at: {9U, 10U, 11U, 31U}) {
        ZEST_CONTEXT("byte {}", at);
        ZEXPECT(bytes[at] == std::byte{0});
    }
}

ZEST_CASE(empty_parts_take_no_bytes) {
    auto bytes = test::blob_of(test::empty_probe());
    ZASSERT(bytes.size() == 28U);
    for(std::size_t at: {12U, 16U, 20U, 24U}) {
        ZEST_CONTEXT("word at {}", at);
        ZEXPECT(u32_at(bytes, at) == 0U);
    }
}

ZEST_CASE(padding_is_zeroed) {
    // Whatever a value's own padding holds, equal values have equal blobs.
    std::vector<test::PaddedPoint> dirty(2);
    std::memset(static_cast<void*>(dirty.data()), 0xab, sizeof(test::PaddedPoint) * dirty.size());
    dirty[0].x = 1;
    dirty[0].flag = true;
    dirty[0].y = 2;
    dirty[1].x = 3;
    dirty[1].flag = false;
    dirty[1].y = 4;
    auto value = test::sample();
    auto clean = test::blob_of(value);
    value.points = dirty;
    ZEXPECT(test::blob_of(value) == clean);
}

ZEST_CASE(bool_sequence_is_a_byte_per_element) {
    // std::vector<bool> packs its bits; a blob holds one byte per bool.
    auto bytes = test::blob_of(test::Flags{
        .flags = {true, false, true}
    });
    ZASSERT(bytes.size() == 19U);
    ZEXPECT(u32_at(bytes, 12) == 3U);
    ZEXPECT(bytes[16] == std::byte{1});
    ZEXPECT(bytes[17] == std::byte{0});
    ZEXPECT(bytes[18] == std::byte{1});
}

};  // ZEST_SUITE(codec_blob_encode)

}  // namespace

}  // namespace kota::codec::blob
