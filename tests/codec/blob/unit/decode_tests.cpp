#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "codec/blob/harness/fixtures.h"
#include "kota/zest/zest.h"
#include "kota/codec/blob/blob.h"

namespace kota::codec::blob {

namespace {

/// Alike in fields and size, but for where `third` sits.
struct Near {
    std::uint64_t first = 0;
    std::uint16_t second = 0;
    std::uint16_t third = 0;
};

struct Far {
    std::uint64_t first = 0;
    std::uint16_t second = 0;
    alignas(4) std::uint16_t third = 0;
};

static_assert(sizeof(Near) == sizeof(Far));

enum class Toggle : bool {
    Off,
    On,
};

struct Switch {
    std::uint32_t id = 0;
    Toggle toggle = Toggle::Off;
};

/// Bytes at an address `shift` past a multiple of the alignment.
struct Shifted {
    std::vector<std::uint64_t> words;

    std::span<const std::byte> bytes;

    Shifted(std::span<const std::byte> from, std::size_t shift) :
        words(from.size() / sizeof(std::uint64_t) + 2) {
        auto* at = reinterpret_cast<std::byte*>(words.data()) + shift;
        std::memcpy(at, from.data(), from.size());
        bytes = {at, from.size()};
    }
};

void set_u32(std::vector<std::byte>& bytes, std::size_t at, std::uint32_t value) {
    std::memcpy(bytes.data() + at, &value, sizeof(value));
}

/// Where `part`, inside the root view of `bytes`, sits in them.
std::size_t offset_of(std::span<const std::byte> bytes, const void* part) {
    return static_cast<std::size_t>(static_cast<const std::byte*>(part) - bytes.data());
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
    ZEXPECT(root.first == 11U);
    ZEXPECT(root.second == 22U);
    ZEXPECT(std::string_view(root.text) == "hello");
    ZEXPECT(root.empty_text.empty());
    ZEXPECT(
        std::ranges::equal(std::span<const std::uint32_t>(root.numbers), std::array{1U, 2U, 3U}));
    ZEXPECT(root.empty_numbers.empty());

    const auto& names = root.names;
    ZASSERT(names.size() == 3U);
    ZEXPECT(names[0].str() == "a");
    ZEXPECT(names[1].empty());
    ZEXPECT(names.back().str() == "ccc");

    ZASSERT(root.points.size() == 2U);
    ZEXPECT(root.points.front().flag);
    ZEXPECT(root.points[1].y == 4U);

    const auto& leaves = root.leaves;
    ZASSERT(leaves.size() == 3U);
    ZEXPECT(leaves[0].name.str() == "x");
    ZEXPECT(std::ranges::equal(leaves[0].values, std::array<std::uint16_t, 2>{1, 2}));
    ZEXPECT(leaves[1].values.empty());
    ZEXPECT(leaves[2].name.str() == "zz");

    ZEXPECT(root.inner.id == 42U);
    ZEXPECT(root.inner.label.str() == "inner");
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
    out.names.emplace_back("more");
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
    auto decoded = from_bytes<test::Probe>(octets);
    ZASSERT(decoded);
    ZEXPECT(*decoded == test::probe());
}

ZEST_CASE(plain_sequences_sit_aligned_in_the_bytes) {
    auto bytes = test::blob_of(test::sample());
    auto viewed = view<test::Sample>(bytes);
    ZASSERT(viewed);
    const auto& root = **viewed;
    auto inside = [&](const void* data) {
        auto at = offset_of(bytes, data);
        ZEST_CONTEXT("offset {}", at);
        ZEXPECT(at < bytes.size());
        ZEXPECT(at % alignment == 0U);
    };
    inside(root.numbers.data());
    inside(root.points.data());
    inside(root.bytes.data());
}

ZEST_CASE(nested_blob_reads_in_place) {
    // A blob held as the bytes of another one is aligned, so it reads where
    // it lies.
    auto inner = test::blob_of(test::Inner{.id = 7, .label = "seven"});
    test::Sample outer;
    outer.bytes.resize(inner.size());
    std::memcpy(outer.bytes.data(), inner.data(), inner.size());
    auto bytes = test::blob_of(outer);
    auto viewed = view<test::Sample>(bytes);
    ZASSERT(viewed);
    const auto& held = (*viewed)->bytes;
    auto nested = view<test::Inner>(std::span(held.data(), held.size()));
    ZASSERT(nested);
    ZEXPECT((*nested)->label.str() == "seven");
}

ZEST_CASE(empty_vec_has_null_data) {
    auto bytes = test::blob_of(test::empty_probe());
    auto viewed = view<test::Probe>(bytes);
    ZASSERT(viewed);
    // Pointers compare as themselves.
    ZEXPECT(((*viewed)->numbers.data() == nullptr));
    ZEXPECT(((*viewed)->numbers.begin() == (*viewed)->numbers.end()));
    ZEXPECT(std::string_view((*viewed)->text).empty());
}

ZEST_CASE(view_verified_reads_the_same_root) {
    auto bytes = test::blob_of(test::sample());
    auto viewed = view<test::Sample>(bytes);
    ZASSERT(viewed);
    ZEXPECT((&view_verified<test::Sample>(bytes) == *viewed));
}

ZEST_CASE(misaligned_bytes_fail) {
    auto bytes = test::blob_of(test::sample());
    Shifted shifted(bytes, 1);
    auto viewed = view<test::Sample>(shifted.bytes);
    ZASSERT(!viewed);
    ZEXPECT(viewed.error().message == "the bytes do not start at an 8-byte boundary");
}

ZEST_CASE(cut_bytes_fail) {
    auto bytes = test::blob_of(test::sample());
    for(std::size_t size = 0; size < bytes.size(); ++size) {
        ZEST_CONTEXT("{} bytes of {}", size, bytes.size());
        ZEXPECT(!view<test::Sample>(std::span(bytes).first(size)));
    }
    auto viewed = view<test::Sample>(std::span(bytes).first(8));
    ZASSERT(!viewed);
    ZEXPECT(viewed.error().message == "the bytes cannot hold a blob of this type");
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

ZEST_CASE(another_layout_fails) {
    auto viewed = view<test::Inner>(test::blob_of(test::sample()));
    ZASSERT(!viewed);
    ZEXPECT(viewed.error().message == "a blob of another layout");
}

ZEST_CASE(moved_field_fails) {
    // The fingerprint holds every field's offset.
    auto viewed = view<Near>(test::blob_of(Far{.first = 1, .second = 2, .third = 3}));
    ZASSERT(!viewed);
    ZEXPECT(viewed.error().message == "a blob of another layout");
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

ZEST_CASE(misplaced_part_fails) {
    // The string's bytes start at 28, 16 past its Vec at 12.
    auto bytes = test::blob_of(test::probe());
    set_u32(bytes, 12, 17);
    auto viewed = view<test::Probe>(bytes);
    ZASSERT(!viewed);
    ZEXPECT(viewed.error().message == "a part is not where the layout puts it");
    ZEXPECT(viewed.error().format_path() == "text");
}

ZEST_CASE(wrong_length_misplaces_the_next_part) {
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

ZEST_CASE(error_path_names_the_element) {
    auto original = test::blob_of(test::sample());
    auto viewed = view<test::Sample>(original);
    ZASSERT(viewed);
    const auto& root = **viewed;

    auto name = original;
    set_u32(name, offset_of(original, &root.leaves[2].name), 1);
    auto misplaced = view<test::Sample>(name);
    ZASSERT(!misplaced);
    ZEXPECT(misplaced.error().format_path() == "leaves[2].name");

    auto flag = original;
    flag[offset_of(original, &root.points[1].flag)] = std::byte{7};
    auto bad_bool = view<test::Sample>(flag);
    ZASSERT(!bad_bool);
    ZEXPECT(bad_bool.error().format_path() == "points[1].flag");
}

};  // ZEST_SUITE(codec_blob_decode)

}  // namespace

}  // namespace kota::codec::blob
