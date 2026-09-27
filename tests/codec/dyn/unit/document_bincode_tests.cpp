#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/codec/bincode/bincode.h"
#include "kota/codec/dyn/document.h"

// dyn::Value's bincode form, declared with the type in document.h: a kind
// byte, then what the value holds.

namespace kota::codec {

namespace {

std::vector<std::byte> bytes(std::initializer_list<unsigned> values) {
    std::vector<std::byte> out;
    for(unsigned value: values) {
        out.push_back(static_cast<std::byte>(value));
    }
    return out;
}

/// Every kind, nested.
dyn::Value every_kind() {
    return dyn::Value{
        {"null",   nullptr                                                       },
        {"flag",   true                                                          },
        {"signed", std::int64_t{-7}                                              },
        {"big",    std::uint64_t{1} << 63                                        },
        {"ratio",  2.5                                                           },
        {"text",   "x"                                                           },
        {"list",   dyn::Array{std::int64_t{1}, dyn::Value("two"), dyn::Array{}}  },
        {"inner",  dyn::Object{{"deep", dyn::Object{{"leaf", std::uint64_t{3}}}}}},
    };
}

ZEST_SUITE(codec_dyn_document_bincode) {

ZEST_CASE(value_writes_its_kind_first) {
    auto null = bincode::to_bytes(dyn::Value(nullptr));
    ASSERT(null);
    EXPECT(*null == bytes({0x00}));

    auto flag = bincode::to_bytes(dyn::Value(true));
    ASSERT(flag);
    EXPECT(*flag == bytes({0x01, 0x01}));

    auto number = bincode::to_bytes(dyn::Value(std::int64_t{-1}));
    ASSERT(number);
    EXPECT(*number == bytes({0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}));

    // An object: kind, entry count, then each key and tagged value.
    auto object = bincode::to_bytes(dyn::Value{
        {"a", std::uint64_t{5}}
    });
    ASSERT(object);
    EXPECT(*object == bytes({0x07, 1, 0, 0,   0,    0, 0, 0, 0, 1, 0, 0, 0, 0,
                             0,    0, 0, 'a', 0x03, 5, 0, 0, 0, 0, 0, 0, 0}));
}

ZEST_CASE(every_kind_roundtrip) {
    auto encoded = bincode::to_bytes(every_kind());
    ASSERT(encoded);
    auto decoded = bincode::from_bytes<dyn::Value>(std::span<const std::byte>(*encoded));
    ASSERT(decoded);
    EXPECT(*decoded == every_kind());
}

ZEST_CASE(unknown_kind_fails) {
    auto decoded = bincode::from_bytes<dyn::Value>(std::span<const std::byte>(bytes({0x08})));
    ASSERT(!decoded);
    EXPECT(decoded.error().message == "invalid dyn::Value kind 8");
}

ZEST_CASE(missing_kind_fails) {
    auto decoded = bincode::from_bytes<dyn::Value>(std::span<const std::byte>());
    ASSERT(!decoded);
    EXPECT(decoded.error().message == "unexpected eof");
}

};  // ZEST_SUITE(codec_dyn_document_bincode)

}  // namespace

}  // namespace kota::codec
