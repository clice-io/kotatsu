#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/codec/bincode/bincode.h"
#include "kota/codec/dyn/document.h"

// dyn::Value in bincode: a kind byte, then what the value holds.

namespace kota::codec {

namespace {

std::vector<std::byte> bytes(std::initializer_list<unsigned> values) {
    std::vector<std::byte> out;
    for(unsigned value: values) {
        out.push_back(static_cast<std::byte>(value));
    }
    return out;
}

/// `levels` arrays each holding the next, around a null: a kind byte and a
/// one-element count per array.
std::vector<std::byte> nested_arrays(std::size_t levels) {
    std::vector<std::byte> out;
    for(std::size_t i = 0; i < levels; ++i) {
        auto level = bytes({0x06, 1, 0, 0, 0, 0, 0, 0, 0});
        out.insert(out.end(), level.begin(), level.end());
    }
    out.push_back(std::byte{0x00});
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

ZEST_SUITE(codec_bincode_dyn_value) {

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

ZEST_CASE(object_roundtrip) {
    dyn::Object object{
        {"b", std::int64_t{2} },
        {"a", dyn::Array{true}}
    };
    auto encoded = bincode::to_bytes(object);
    ASSERT(encoded);
    auto decoded = bincode::from_bytes<dyn::Object>(std::span<const std::byte>(*encoded));
    ASSERT(decoded);
    EXPECT(*decoded == object);
}

ZEST_CASE(object_entry_error_names_its_index) {
    // One entry: key "a", then a value of unknown kind.
    auto decoded = bincode::from_bytes<dyn::Object>(std::span<const std::byte>(
        bytes({1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 'a', 0x09})));
    ASSERT(!decoded);
    EXPECT(decoded.error().message == "invalid dyn::Value kind 9");
    EXPECT(decoded.error().format_path() == "[0]");
}

ZEST_CASE(nested_arrays_roundtrip) {
    // Deep, but well within the limit: a deeper tree is left to the
    // failure cases, since a Value's destructor recurses.
    auto document = nested_arrays(100);
    auto decoded = bincode::from_bytes<dyn::Value>(std::span<const std::byte>(document));
    ASSERT(decoded);
    auto again = bincode::to_bytes(*decoded);
    ASSERT(again);
    EXPECT(*again == document);
}

ZEST_CASE(nesting_past_the_limit_fails) {
    // 1024 arrays put the null 1025 Values deep.
    auto document = nested_arrays(1024);
    auto decoded = bincode::from_bytes<dyn::Value>(std::span<const std::byte>(document));
    ASSERT(!decoded);
    EXPECT(decoded.error().message == "dyn::Value nested deeper than 1024 levels");
    EXPECT(decoded.error().path.size() == 1024);
}

ZEST_CASE(hostile_nesting_fails) {
    // 40000 nested arrays, 360 KB: far past any stack a recursive read
    // could use, and past the limit long before the end.
    auto document = nested_arrays(40000);
    auto decoded = bincode::from_bytes<dyn::Value>(std::span<const std::byte>(document));
    ASSERT(!decoded);
    EXPECT(decoded.error().message == "dyn::Value nested deeper than 1024 levels");
}

ZEST_CASE(missing_kind_fails) {
    auto decoded = bincode::from_bytes<dyn::Value>(std::span<const std::byte>());
    ASSERT(!decoded);
    EXPECT(decoded.error().message == "unexpected eof");
}

};  // ZEST_SUITE(codec_bincode_dyn_value)

}  // namespace

}  // namespace kota::codec
