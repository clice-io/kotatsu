#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
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

ZEST_CASE(lengths_take_the_fewest_bytes) {
    // Below 251, the length itself; then a marker and two, four or eight
    // bytes. Counts are written the same way.
    struct Expected {
        std::size_t size;
        std::vector<std::byte> prefix;
    };

    const Expected cases[] = {
        {.size = 0,     .prefix = {std::byte{0}}                                           },
        {.size = 250,   .prefix = {std::byte{250}}                                         },
        {.size = 251,   .prefix = {std::byte{251}, std::byte{251}, std::byte{0}}           },
        {.size = 65535, .prefix = {std::byte{251}, std::byte{0xFF}, std::byte{0xFF}}       },
        {.size = 65536,
         .prefix = {std::byte{252}, std::byte{0}, std::byte{0}, std::byte{1}, std::byte{0}}},
    };
    for(const auto& expected: cases) {
        ZEST_CONTEXT("size {}", expected.size);
        auto text = bincode::to_bytes(std::string(expected.size, 'x'));
        ZASSERT(text);
        ZASSERT(text->size() == expected.prefix.size() + expected.size);
        ZEXPECT(std::vector(text->begin(), text->begin() + expected.prefix.size()) ==
                expected.prefix);
        auto list = bincode::to_bytes(std::vector<bool>(expected.size));
        ZASSERT(list);
        ZASSERT(list->size() >= expected.prefix.size());
        ZEXPECT(std::vector(list->begin(), list->begin() + expected.prefix.size()) ==
                expected.prefix);
    }
}

ZEST_CASE(write_length_takes_each_width_reader_reads) {
    struct Expected {
        std::uint64_t length;
        std::size_t size;
    };

    const Expected cases[] = {
        {.length = 0,                     .size = 1},
        {.length = 250,                   .size = 1},
        {.length = 251,                   .size = 3},
        {.length = 65535,                 .size = 3},
        {.length = 65536,                 .size = 5},
        {.length = 0xFFFF'FFFF,           .size = 5},
        {.length = 0x1'0000'0000,         .size = 9},
        {.length = 0xFFFF'FFFF'FFFF'FFFF, .size = 9},
    };
    for(const auto& expected: cases) {
        ZEST_CONTEXT("length {}", expected.length);
        std::vector<std::byte> buf;
        bincode::Writer writer{buf};
        writer.write_length(expected.length);
        ZEXPECT(writer.size == expected.size);
        bincode::Reader reader{std::span<const std::byte>(buf.data(), writer.size)};
        std::uint64_t length = 0;
        ZASSERT(reader.read_length(length));
        ZEXPECT(length == expected.length);
        ZEXPECT(reader.pos == expected.size);
    }
}

};  // ZEST_SUITE(codec_bincode_encode)

}  // namespace

}  // namespace kota::codec
