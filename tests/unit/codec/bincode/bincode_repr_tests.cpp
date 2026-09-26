#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/meta/repr.h"
#include "kota/codec/bincode/bincode.h"

namespace kota_bincode_repr_test {

enum class flavor : std::uint8_t {
    plain,
    spicy,
};

// The roaring::Roaring pattern: a third-party value class that frames itself
// as a byte blob. The shape is declared dynamic; on a streaming backend the
// user body stays symmetric by writing/reading one length-prefixed vector.
struct blob_bag {
    std::vector<std::byte> bytes;

    auto operator==(const blob_bag&) const -> bool = default;
};

}  // namespace kota_bincode_repr_test

namespace kota::meta {

template <>
struct repr<kota_bincode_repr_test::flavor> {
    using type = std::uint32_t;

    // Non-identity mapping: bypassing the repr would be byte-visible.
    static type to(kota_bincode_repr_test::flavor f) {
        return static_cast<type>(f) + 100;
    }

    static kota_bincode_repr_test::flavor from(type v) {
        return static_cast<kota_bincode_repr_test::flavor>(v - 100);
    }
};

template <>
struct repr<kota_bincode_repr_test::blob_bag> {
    using type = dynamic;

    // Self-framed: a magic prefix then one length-prefixed byte vector, so a
    // dispatch regression (struct framing instead) changes the byte count.
    template <typename Config>
    static bool serialize(auto& vis, const kota_bincode_repr_test::blob_bag& b) {
        if(!codec::encode_value<Config>(vis, std::uint8_t{0x42}))
            return false;
        return codec::encode_value<Config>(vis, b.bytes);
    }

    template <typename Config>
    static bool deserialize(auto& vis, kota_bincode_repr_test::blob_bag& b) {
        std::uint8_t magic = 0;
        if(!codec::decode_value<Config>(vis, magic) || magic != 0x42)
            return false;
        std::vector<std::byte> bytes;
        if(!codec::decode_value<Config>(vis, bytes))
            return false;
        b.bytes = std::move(bytes);
        return true;
    }
};

}  // namespace kota::meta

namespace kota::codec {

namespace {

using kota_bincode_repr_test::blob_bag;
using kota_bincode_repr_test::flavor;

struct order {
    flavor f = flavor::plain;
    blob_bag payload;
    std::string note;

    auto operator==(const order&) const -> bool = default;
};

ZEST_SUITE(serde_bincode_repr) {

ZEST_CASE(declarative_and_dynamic_repr_roundtrip) {
    const order input{
        .f = flavor::spicy,
        .payload = blob_bag{{std::byte{0xAB}, std::byte{0xCD}}},
        .note = "extra",
    };

    auto bytes = bincode::to_bytes(input);
    ASSERT(bytes);

    order output{};
    auto status = bincode::from_bytes(*bytes, output);
    ASSERT(status);
    EXPECT(output == input);
}

ZEST_CASE(repr_dispatch_is_byte_visible) {
    // The enum travels as its mapped uint32 encoded value, byte-identical to
    // encoding that encoded value directly.
    auto via_repr = bincode::to_bytes(flavor::spicy);
    auto encoded = bincode::to_bytes(std::uint32_t{101});
    ASSERT(via_repr);
    ASSERT(encoded);
    EXPECT(*via_repr == *encoded);

    // magic (u64-widened) + length prefix (u64) + 2 payload bytes.
    auto blob = bincode::to_bytes(blob_bag{
        {std::byte{0x01}, std::byte{0x02}}
    });
    ASSERT(blob);
    EXPECT(blob->size() == 18U);
}

ZEST_CASE(repr_reaches_sequence_elements) {
    const std::vector<flavor> input{flavor::spicy, flavor::plain, flavor::spicy};

    auto bytes = bincode::to_bytes(input);
    ASSERT(bytes);

    std::vector<flavor> output;
    auto status = bincode::from_bytes(*bytes, output);
    ASSERT(status);
    EXPECT(output == input);
}

};  // ZEST_SUITE(serde_bincode_repr)

}  // namespace

}  // namespace kota::codec

// ============================================================================
// Format-scoped repr: the bincode backend picks repr<T, bincode::format>.
// ============================================================================

namespace kota_bincode_format_test {

// Generic form is textual; the bincode-scoped override is a bare uint32.
// bincode is not self-describing, so writer and reader silently corrupt data
// unless both resolve the same scoped repr — the shape-twin decode below
// pins the wire form itself.
class channel_id {
public:
    channel_id() = default;

    explicit channel_id(std::uint32_t v) : value_(v) {}

    auto value() const -> std::uint32_t {
        return value_;
    }

    auto operator==(const channel_id&) const -> bool = default;

private:
    std::uint32_t value_ = 0;
};

struct beacon {
    channel_id id;

    auto operator==(const beacon&) const -> bool = default;
};

// Same wire shape as beacon under the scoped repr.
struct beacon_twin {
    std::uint32_t id = 0;
};

}  // namespace kota_bincode_format_test

namespace kota::meta {

template <>
struct repr<kota_bincode_format_test::channel_id> {
    using type = std::string;

    static type to(const kota_bincode_format_test::channel_id& id) {
        return "c" + std::to_string(id.value());
    }

    static kota_bincode_format_test::channel_id from(const std::string& encoded) {
        return kota_bincode_format_test::channel_id{
            static_cast<std::uint32_t>(std::stoul(encoded.substr(1)))};
    }
};

template <>
struct repr<kota_bincode_format_test::channel_id, codec::bincode::format> {
    using type = std::uint32_t;

    static type to(const kota_bincode_format_test::channel_id& id) {
        return id.value();
    }

    static kota_bincode_format_test::channel_id from(type v) {
        return kota_bincode_format_test::channel_id{v};
    }
};

}  // namespace kota::meta

namespace kota::codec {

namespace {

using kota_bincode_format_test::beacon;
using kota_bincode_format_test::beacon_twin;
using kota_bincode_format_test::channel_id;

ZEST_SUITE(serde_bincode_format_scoped) {

ZEST_CASE(format_scoped_repr_selected_by_bincode) {
    const beacon input{.id = channel_id{7}};

    auto encoded = bincode::to_bytes(input);
    ASSERT(encoded);

    // The bytes hold the scoped uint32, not the length-prefixed string form.
    auto twin = bincode::from_bytes<beacon_twin>(*encoded);
    ASSERT(twin);
    EXPECT(twin->id == 7U);

    auto output = bincode::from_bytes<beacon>(*encoded);
    ASSERT(output);
    EXPECT(*output == input);
}

};  // ZEST_SUITE(serde_bincode_format_scoped)

}  // namespace

}  // namespace kota::codec
