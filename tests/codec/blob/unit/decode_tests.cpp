#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include "codec/blob/harness/fixtures.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/codec/blob/blob.h"

namespace kotatsu::codec::blob {

namespace {

struct Unsigned {
    std::uint32_t x = 0;
};

struct Renamed {
    std::uint32_t y = 0;
};

struct Signed {
    std::int32_t x = 0;
};

struct Real {
    float x = 0;
};

template <template <typename> typename Seq>
struct TextOf {
    Seq<char> x;
};

template <template <typename> typename Seq>
struct OctetsOf {
    Seq<std::uint8_t> x;
};

/// Alike in fields and size, but for where `third` sits.
struct Near {
    std::uint64_t first = 0;
    std::uint16_t second = 0;
    std::uint16_t third = 0;
};

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)  // padded to its alignment, which is the point
#endif
struct Far {
    std::uint64_t first = 0;
    std::uint16_t second = 0;
    alignas(4) std::uint16_t third = 0;
};

/// Unsigned with tail padding.
struct alignas(8) Wider {
    std::uint32_t x = 0;
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

static_assert(sizeof(Near) == sizeof(Far));

enum class Toggle : bool {
    Off,
    On,
};

struct Switch {
    std::uint32_t id = 0;
    Toggle toggle = Toggle::Off;
};

void set_u32(std::vector<std::byte>& bytes, std::size_t at, std::uint32_t value) {
    std::memcpy(bytes.data() + at, &value, sizeof(value));
}

/// Where `part`, inside the root view of `bytes`, sits in them. The tests
/// change a copy at that offset: changed through `bytes` themselves, GCC
/// follows the pointer back to the Vec it came from and reports a write past
/// it (-Wstringop-overflow).
std::size_t offset_of(std::span<const std::byte> bytes, const void* part) {
    return static_cast<std::size_t>(static_cast<const std::byte*>(part) - bytes.data());
}

/// The blob of `value` viewed as an As, which a layout that differs in `what`
/// refuses.
template <typename As, typename T>
void another_layout_fails(const T& value, std::string_view what) {
    ZEST_CONTEXT("{}", what);
    auto viewed = view<As>(test::blob_of(value));
    ZASSERT(!viewed);
    ZEXPECT(viewed.error().message == "a blob of another layout");
}

ZEST_SUITE(codec_blob_decode) {

ZEST_CASE(view_reads_fields_in_place) {
    auto value = test::sample();
    auto bytes = test::blob_of(value);
    auto viewed = view<test::Sample>(bytes);
    ZASSERT(viewed);
    const auto& root = **viewed;
    ZEXPECT(root.enabled);
    ZEXPECT(root.small == 7);
    ZEXPECT(root.negative == -5);
    ZEXPECT(root.wide == (1ull << 40));
    ZEXPECT(root.ratio == 0.5f);
    ZEXPECT(root.precise == 2.25);
    ZEXPECT(root.color == test::Shade::Green);
    ZEXPECT(root.stamp.seconds == 11U);
    ZEXPECT(root.stamp.nanos == 22U);
    ZEXPECT(std::string_view(root.text) == "hello");
    ZEXPECT(root.empty_text.empty());
    ZEXPECT(
        std::ranges::equal(std::span<const std::uint32_t>(root.numbers), std::array{1U, 2U, 3U}));
    ZEXPECT(root.empty_numbers.empty());

    const auto& names = root.names;
    ZASSERT(names.size() == 3U);
    ZEXPECT(std::string_view(names[0]) == "a");
    ZEXPECT(names[1].empty());
    ZEXPECT(std::string_view(names.back()) == "ccc");

    ZEXPECT(std::vector<test::PaddedPoint>(root.points.begin(), root.points.end()) == value.points);

    ZASSERT(root.stamps.size() == 2U);
    ZEXPECT(root.stamps.front().seconds == 5U);
    ZEXPECT(root.stamps.back().nanos == 8U);

    const auto& leaves = root.leaves;
    ZASSERT(leaves.size() == 3U);
    ZEXPECT(std::string_view(leaves[0].name) == "x");
    ZEXPECT(std::ranges::equal(leaves[0].values, std::array<std::uint16_t, 2>{1, 2}));
    ZEXPECT(leaves[1].values.empty());
    ZEXPECT(std::string_view(leaves[2].name) == "zz");

    ZEXPECT(root.badge.id == 42U);
    ZEXPECT(std::string_view(root.badge.label) == "badge");
}

ZEST_CASE(from_bytes_roundtrip) {
    auto value = test::sample();
    auto decoded = from_bytes<test::Sample>(test::blob_of(value));
    ZASSERT(decoded);
    ZEXPECT(*decoded == value);
}

ZEST_CASE(from_bytes_replaces_what_out_holds) {
    auto value = test::sample();
    auto out = test::sample();
    out.text = "other";
    out.empty_text = "stale";
    out.empty_numbers = {9};
    out.names.emplace_back("more");
    out.stamps.clear();
    out.leaves.clear();
    out.bytes.clear();
    auto status = from_bytes(test::blob_of(value), out);
    ZASSERT(status);
    ZEXPECT(out == value);
}

ZEST_CASE(bool_sequence_roundtrip) {
    test::Flags value{
        .flags = {true, false, true}
    };
    auto decoded = from_bytes<test::Flags>(test::blob_of(value));
    ZASSERT(decoded);
    ZEXPECT(decoded->flags == value.flags);
}

ZEST_CASE(byte_spans_read_alike) {
    auto bytes = test::blob_of(test::probe());
    std::span<const std::uint8_t> octets(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                         bytes.size());
    auto viewed = view<test::Probe>(octets);
    ZASSERT(viewed);
    ZEXPECT(std::string_view((*viewed)->text) == "abc");
    ZEXPECT((&view_verified<test::Probe>(octets) == *viewed));
    auto decoded = from_bytes<test::Probe>(octets);
    ZASSERT(decoded);
    ZEXPECT(*decoded == test::probe());
    test::Probe out;
    ZASSERT(from_bytes(octets, out));
    ZEXPECT(out == test::probe());
}

ZEST_CASE(nested_blob_reads_in_place) {
    // A blob held as the bytes of another one is aligned, whatever string
    // comes before it, so it reads where it lies.
    auto inner = test::blob_of(test::Badge{.id = 7, .label = "seven"});
    test::Sample outer;
    outer.text = "odd";
    outer.bytes.resize(inner.size());
    std::memcpy(outer.bytes.data(), inner.data(), inner.size());
    auto bytes = test::blob_of(outer);
    auto viewed = view<test::Sample>(bytes);
    ZASSERT(viewed);
    const auto& held = (*viewed)->bytes;
    auto nested = view<test::Badge>(std::span(held.data(), held.size()));
    ZASSERT(nested);
    ZEXPECT(std::string_view((*nested)->label) == "seven");
}

ZEST_CASE(view_verified_reads_the_same_root) {
    auto bytes = test::blob_of(test::sample());
    auto viewed = view<test::Sample>(bytes);
    ZASSERT(viewed);
    ZEXPECT((&view_verified<test::Sample>(bytes) == *viewed));
}

ZEST_CASE(from_bytes_of_another_layout_fails) {
    auto bytes = test::blob_of(test::sample());
    auto decoded = from_bytes<test::Badge>(bytes);
    ZASSERT(!decoded);
    ZEXPECT(decoded.error().message == "a blob of another layout");
    test::Badge out;
    auto status = from_bytes(bytes, out);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "a blob of another layout");
    std::span<const std::uint8_t> octets(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                         bytes.size());
    auto from_octets = from_bytes<test::Badge>(octets);
    ZASSERT(!from_octets);
    ZEXPECT(from_octets.error().message == "a blob of another layout");
}

ZEST_CASE(misaligned_bytes_fails) {
    auto bytes = test::blob_of(test::sample());
    std::vector<std::uint64_t> words(bytes.size() / sizeof(std::uint64_t) + 2);
    auto* shifted = reinterpret_cast<std::byte*>(words.data()) + 1;
    std::memcpy(shifted, bytes.data(), bytes.size());
    auto viewed = view<test::Sample>(std::span(shifted, bytes.size()));
    ZASSERT(!viewed);
    ZEXPECT(viewed.error().message == "the bytes do not start at an 8-byte boundary");
}

ZEST_CASE(cut_bytes_fails) {
    auto bytes = test::blob_of(test::sample());
    for(std::size_t size = 0; size < bytes.size(); ++size) {
        ZEST_CONTEXT("{} bytes of {}", size, bytes.size());
        ZEXPECT(!view<test::Sample>(std::span(bytes).first(size)));
    }
    // The header and the root, and one byte short of them.
    constexpr auto inline_size = 8 + sizeof(view_t<test::Sample>);
    auto short_of_root = view<test::Sample>(std::span(bytes).first(inline_size - 1));
    ZASSERT(!short_of_root);
    ZEXPECT(short_of_root.error().message == "the bytes cannot hold a blob of this type");
    auto root_alone = view<test::Sample>(std::span(bytes).first(inline_size));
    ZASSERT(!root_alone);
    ZEXPECT(root_alone.error().message == "a part runs past the end of the bytes");
    ZEXPECT(root_alone.error().format_path() == "text");
}

ZEST_CASE(trailing_byte_fails) {
    auto bytes = test::blob_of(test::sample());
    bytes.push_back(std::byte{0});
    auto viewed = view<test::Sample>(bytes);
    ZASSERT(!viewed);
    ZEXPECT(viewed.error().message == "bytes past the blob's last part");
}

ZEST_CASE(junk_fails) {
    std::vector<std::byte> junk(64, std::byte{'x'});
    auto viewed = view<test::Probe>(junk);
    ZASSERT(!viewed);
    ZEXPECT(viewed.error().message == "not a blob");
}

ZEST_CASE(another_root_fails) {
    another_layout_fails<test::Badge>(test::sample(), "another root");
}

ZEST_CASE(one_part_of_the_layout_differs_fails) {
    // The fingerprint holds every field's name, offset and kind, and every
    // record's size.
    another_layout_fails<Unsigned>(Renamed{.y = 1}, "a field's name");
    another_layout_fails<Near>(Far{.first = 1, .second = 2, .third = 3}, "a field's offset");
    another_layout_fails<Unsigned>(Signed{.x = 1}, "a scalar's signedness");
    another_layout_fails<Unsigned>(Real{.x = 1}, "an integer or a floating scalar");
    another_layout_fails<TextOf<Owned>>(OctetsOf<Owned>{.x = {1}}, "a string or a byte sequence");
    another_layout_fails<Unsigned>(Wider{.x = 1}, "a record's size");
}

ZEST_CASE(bool_byte_beyond_one_fails) {
    auto bytes = test::blob_of(test::probe());
    bytes[8] = std::byte{2};
    auto viewed = view<test::Probe>(bytes);
    ZASSERT(!viewed);
    ZEXPECT(viewed.error().message == "a bool is neither 0 nor 1");
    ZEXPECT(viewed.error().format_path() == "flag");
}

ZEST_CASE(enum_over_bool_byte_beyond_one_fails) {
    auto bytes = test::blob_of(Switch{.id = 1, .toggle = Toggle::On});
    bytes[12] = std::byte{2};
    auto viewed = view<Switch>(bytes);
    ZASSERT(!viewed);
    ZEXPECT(viewed.error().message == "a bool is neither 0 nor 1");
    ZEXPECT(viewed.error().format_path() == "toggle");
}

ZEST_CASE(bool_element_beyond_one_fails) {
    // The three bools sit at 16, after the root's Vec.
    auto bytes = test::blob_of(test::Flags{
        .flags = {true, false, true}
    });
    bytes[17] = std::byte{2};
    auto viewed = view<test::Flags>(bytes);
    ZASSERT(!viewed);
    ZEXPECT(viewed.error().message == "a bool is neither 0 nor 1");
    ZEXPECT(viewed.error().format_path() == "flags[1]");
}

ZEST_CASE(misplaced_part_fails) {
    // The string's bytes start at 28, 16 past its Vec at 12.
    auto bytes = test::blob_of(test::probe());
    set_u32(bytes, 12, 17);
    auto viewed = view<test::Probe>(bytes);
    ZASSERT(!viewed);
    ZEXPECT(viewed.error().message == "a part is not where the layout puts it");
    ZEXPECT(viewed.error().format_path() == "text");
}

ZEST_CASE(overlong_string_fails) {
    // A longer string moves where the sequence after it must start.
    auto bytes = test::blob_of(test::probe());
    set_u32(bytes, 16, 6);
    auto viewed = view<test::Probe>(bytes);
    ZASSERT(!viewed);
    ZEXPECT(viewed.error().message == "a part is not where the layout puts it");
    ZEXPECT(viewed.error().format_path() == "numbers");
}

ZEST_CASE(part_past_the_end_fails) {
    auto bytes = test::blob_of(test::probe());
    set_u32(bytes, 24, 1U << 30);
    auto viewed = view<test::Probe>(bytes);
    ZASSERT(!viewed);
    ZEXPECT(viewed.error().message == "a part runs past the end of the bytes");
    ZEXPECT(viewed.error().format_path() == "numbers");
}

ZEST_CASE(empty_part_with_an_offset_fails) {
    auto bytes = test::blob_of(test::empty_probe());
    set_u32(bytes, 20, 8);
    auto viewed = view<test::Probe>(bytes);
    ZASSERT(!viewed);
    ZEXPECT(viewed.error().message == "an empty part has an offset");
    ZEXPECT(viewed.error().format_path() == "numbers");
}

ZEST_CASE(misplaced_string_element_fails) {
    auto bytes = test::blob_of(test::sample());
    auto viewed = view<test::Sample>(bytes);
    ZASSERT(viewed);
    ZASSERT((*viewed)->names.size() == 3U);
    auto at = offset_of(bytes, &(*viewed)->names[2]);
    auto mutated = bytes;
    set_u32(mutated, at, 1);
    auto misplaced = view<test::Sample>(mutated);
    ZASSERT(!misplaced);
    ZEXPECT(misplaced.error().message == "a part is not where the layout puts it");
    ZEXPECT(misplaced.error().format_path() == "names[2]");
}

ZEST_CASE(misplaced_element_field_fails) {
    auto bytes = test::blob_of(test::sample());
    auto viewed = view<test::Sample>(bytes);
    ZASSERT(viewed);
    ZASSERT((*viewed)->leaves.size() == 3U);
    auto at = offset_of(bytes, &(*viewed)->leaves[2].name);
    auto mutated = bytes;
    set_u32(mutated, at, 1);
    auto misplaced = view<test::Sample>(mutated);
    ZASSERT(!misplaced);
    ZEXPECT(misplaced.error().message == "a part is not where the layout puts it");
    ZEXPECT(misplaced.error().format_path() == "leaves[2].name");
}

ZEST_CASE(bool_field_of_an_element_fails) {
    auto bytes = test::blob_of(test::sample());
    auto viewed = view<test::Sample>(bytes);
    ZASSERT(viewed);
    ZASSERT((*viewed)->points.size() == 2U);
    auto at = offset_of(bytes, &(*viewed)->points[1].flag);
    auto mutated = bytes;
    mutated[at] = std::byte{7};
    auto bad_bool = view<test::Sample>(mutated);
    ZASSERT(!bad_bool);
    ZEXPECT(bad_bool.error().message == "a bool is neither 0 nor 1");
    ZEXPECT(bad_bool.error().format_path() == "points[1].flag");
}

};  // ZEST_SUITE(codec_blob_decode)

}  // namespace

}  // namespace kotatsu::codec::blob
