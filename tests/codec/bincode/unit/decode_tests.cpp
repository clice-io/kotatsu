#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "codec/harness/fixtures/containers.h"
#include "kota/zest/zest.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"
#include "kota/meta/compare.h"
#include "kota/meta/repr.h"
#include "kota/codec/bincode/bincode.h"

namespace kota::codec {

namespace {

/// Skips its field on decode only.
struct SkipOnDecode {
    constexpr bool operator()(const int& /*value*/, bool is_serialize) const noexcept {
        return !is_serialize;
    }
};

struct Triple {
    int first;
    int second;
    int third;
};

struct SkipsSecondOnDecode {
    int first;
    meta::annotation<int, meta::behavior::skip_if<SkipOnDecode>> second = 88;
    int third;
};

/// A dynamic repr: with no kind to peek at, it frames what it writes, a
/// magic number then the length-prefixed bytes, and reads that framing back.
struct Framed {
    std::vector<std::byte> bytes;

    auto operator==(const Framed&) const -> bool = default;
};

constexpr std::uint8_t framed_magic = 0x42;

/// Much larger in memory than in bincode, which writes it as a number.
struct Page {
    std::array<std::byte, 4096> bytes{};
};

std::vector<std::byte> bytes(std::initializer_list<unsigned> values) {
    std::vector<std::byte> out;
    for(unsigned value: values) {
        out.push_back(static_cast<std::byte>(value));
    }
    return out;
}

}  // namespace

}  // namespace kota::codec

namespace kota::meta {

template <>
struct repr<codec::Page> {
    using type = std::uint8_t;

    static type to(const codec::Page& /*page*/) {
        return 0;
    }

    static codec::Page from(type /*number*/) {
        return {};
    }
};

template <>
struct repr<codec::Framed> {
    using type = dynamic;

    template <typename Config>
    static bool serialize(auto& vis, const codec::Framed& framed) {
        return codec::encode_value<Config>(vis, codec::framed_magic) &&
               codec::encode_value<Config>(vis, framed.bytes);
    }

    template <typename Config>
    static bool deserialize(auto& vis, codec::Framed& framed) {
        std::uint8_t magic = 0;
        return codec::decode_value<Config>(vis, magic) && magic == codec::framed_magic &&
               codec::decode_value<Config>(vis, framed.bytes);
    }
};

}  // namespace kota::meta

namespace kota::codec {

namespace {

/// Whether the value-returning overload takes T.
template <typename T>
concept decodes_by_value =
    requires(std::span<const std::byte> bytes) { bincode::from_bytes<T>(bytes); };

ZEST_SUITE(codec_bincode_decode) {

ZEST_CASE(value_overload_value_initializes) {
    // `T value{}` would copy-list-initialize the explicit list from `{}`.
    const test::HoldsExplicit value{
        .list = {1, 2},
        .count = 2
    };
    auto encoded = bincode::to_bytes(value);
    ZASSERT(encoded);
    auto result = bincode::from_bytes<test::HoldsExplicit>(std::as_bytes(std::span(*encoded)));
    ZASSERT(result);
    const test::HoldsExplicit expected{
        .list = {1, 2},
        .count = 2
    };
    ZEXPECT(meta::eq(*result, expected));
    ZSTATIC_EXPECT(decodes_by_value<test::HoldsExplicit>);
    // A type with no default constructor has no value to decode into.
    ZSTATIC_EXPECT(!decodes_by_value<test::NoDefault>);
}

ZEST_CASE(truncated_payload_fails) {
    auto encoded = bincode::to_bytes(std::string("hello"));
    ZASSERT(encoded);
    std::string out;
    auto status = bincode::from_bytes(std::span<const std::byte>(*encoded).first(4), out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "unexpected eof");
}

ZEST_CASE(oversized_length_prefix_fails) {
    // A length of uint64's maximum with nothing behind it is checked against
    // the bytes left, never used to size a read.
    std::string out;
    auto status =
        bincode::from_bytes(bytes({253, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}), out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "unexpected eof");
}

ZEST_CASE(oversized_element_count_fails) {
    // An element count is not trusted either: it makes room for no more
    // elements than the bytes left, and reading stops at the first element
    // they cannot hold.
    std::vector<int> out;
    auto document = bytes({253, 0, 0, 0, 0, 0, 1, 0, 0});
    for(int element = 0; element < 3; ++element) {
        auto written = bincode::to_bytes(element);
        ZASSERT(written);
        document.insert(document.end(), written->begin(), written->end());
    }
    auto status = bincode::from_bytes(document, out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "unexpected eof");
    ZEXPECT(status.error().format_path() == "[3]");
    ZEXPECT(out.capacity() <= 24U);
}

ZEST_CASE(element_count_makes_room_for_at_most_a_mebibyte) {
    // A page takes 4 KiB of memory but 8 bytes of input: room for as many
    // as the bytes left could hold, 1600, would be 6.25 MiB.
    auto document = bytes({252, 0, 0, 1, 0});
    for(int page = 0; page < 200; ++page) {
        auto written = bincode::to_bytes(Page{});
        ZASSERT(written);
        document.insert(document.end(), written->begin(), written->end());
    }
    std::vector<Page> out;
    auto status = bincode::from_bytes(document, out);
    ZASSERT(!status);
    ZEXPECT(status.error().format_path() == "[200]");
    ZEXPECT(out.capacity() <= (std::size_t{1} << 20) / sizeof(Page));
}

ZEST_CASE(bool_byte_beyond_one_fails) {
    bool out = false;
    auto status = bincode::from_bytes(bytes({2}), out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "type mismatch");
}

ZEST_CASE(option_tag_beyond_one_fails) {
    std::optional<bool> out;
    auto status = bincode::from_bytes(bytes({2, 1}), out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "type mismatch");
}

ZEST_CASE(null_from_non_zero_byte_fails) {
    // A null is written as 0x00; any other byte is not one.
    std::nullptr_t out = nullptr;
    auto status = bincode::from_bytes(bytes({5}), out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "type mismatch");
}

ZEST_CASE(number_out_of_range_fails) {
    auto encoded = bincode::to_bytes(300);
    ZASSERT(encoded);
    std::int8_t out = 0;
    auto status = bincode::from_bytes(*encoded, out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "number out of range");
}

ZEST_CASE(variant_index_out_of_range_fails) {
    std::variant<int, std::string> out;
    auto status = bincode::from_bytes(bytes({2, 0, 0, 0}), out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "invalid variant index 2");
}

ZEST_CASE(trailing_bytes_fails) {
    auto encoded = bincode::to_bytes(7);
    ZASSERT(encoded);
    encoded->push_back(std::byte{0});
    int out = 0;
    auto status = bincode::from_bytes(*encoded, out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "trailing bytes");
}

ZEST_CASE(skip_if_on_decode_reads_past_the_field) {
    // The field's bytes are there, so they are read and dropped, and the
    // next field reads from where it belongs.
    auto encoded = bincode::to_bytes(Triple{.first = 1, .second = 2, .third = 3});
    ZASSERT(encoded);
    SkipsSecondOnDecode out{};
    ZASSERT(bincode::from_bytes(*encoded, out));
    ZEXPECT(out.first == 1);
    ZEXPECT(meta::annotated_value(out.second) == 88);
    ZEXPECT(out.third == 3);
}

ZEST_CASE(dynamic_repr_reads_the_framing_it_writes) {
    // Followed by a string, which reads from where the framing ends.
    std::tuple<Framed, std::string> value{Framed{.bytes = bytes({0xAB, 0xCD})}, "after"};
    auto encoded = bincode::to_bytes(value);
    ZASSERT(encoded);
    auto plain =
        bincode::to_bytes(std::tuple{framed_magic, std::get<0>(value).bytes, std::string("after")});
    ZASSERT(plain);
    ZEXPECT(*encoded == *plain);
    std::tuple<Framed, std::string> out;
    ZASSERT(bincode::from_bytes(*encoded, out));
    ZEXPECT(out == value);
}

ZEST_CASE(optional_of_null_pointer_keeps_its_presence) {
    // The presence byte and the pointer's own null byte both travel, so an
    // engaged optional holding a null pointer is not an empty optional.
    std::optional<std::shared_ptr<int>> value = std::shared_ptr<int>();
    auto encoded = bincode::to_bytes(value);
    ZASSERT(encoded);
    ZEXPECT(*encoded == bytes({1, 0}));
    std::optional<std::shared_ptr<int>> out;
    ZASSERT(bincode::from_bytes(*encoded, out));
    ZASSERT(out.has_value());
    ZEXPECT(*out == nullptr);
}

};  // ZEST_SUITE(codec_bincode_decode)

}  // namespace

}  // namespace kota::codec
