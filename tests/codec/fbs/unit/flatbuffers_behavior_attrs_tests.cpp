#if __has_include(<flatbuffers/flatbuffers.h>)

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"
#include "kota/codec/fbs/fbs.h"
#include "kota/codec/json/json.h"

namespace kota::codec {

using namespace meta;

namespace {

using fbs::from_bytes;
using fbs::to_bytes;

enum class role : std::int32_t {
    admin,
    editor,
    viewer,
};

// Adapter: encode int as its decimal string representation.
struct IntStringAdapter {
    using type = std::string;

    static auto to(int value) -> std::string {
        return std::to_string(value);
    }

    static auto from(std::string encoded) -> int {
        return encoded.empty() ? 0 : std::stoi(encoded);
    }
};

struct with_enum_string_field {
    std::int32_t id = 0;
    annotation<role, behavior::enum_string<rename_policy::identity>> level{role::admin};

    auto operator==(const with_enum_string_field&) const -> bool = default;
};

struct with_adapter_field {
    std::int32_t id = 0;
    annotation<int, behavior::with<IntStringAdapter>> encoded = 0;
    std::string tag;

    auto operator==(const with_adapter_field&) const -> bool = default;
};

struct with_optional_adapter_field {
    std::optional<annotation<int, behavior::with<IntStringAdapter>>> maybe_encoded;

    auto operator==(const with_optional_adapter_field&) const -> bool = default;
};

/// Aggregate underlying type: its annotation inherits from it, so the
/// annotation is itself a trivially-copyable aggregate whose fields
/// reflection walks right through — invisible to any check that only looks
/// at the outermost struct's direct field types.
struct calibration {
    std::int32_t code = 0;

    auto operator==(const calibration&) const -> bool = default;
};

// Adapter: encode calibration as its code's decimal string.
struct CalibrationAdapter {
    using type = std::string;

    static auto to(const calibration& c) -> std::string {
        return std::to_string(c.code);
    }

    static auto from(const std::string& encoded) -> calibration {
        return {.code = encoded.empty() ? 0 : std::stoi(encoded)};
    }
};

struct adapted_probe {
    annotation<calibration, behavior::with<CalibrationAdapter>> cal;

    auto operator==(const adapted_probe&) const -> bool = default;
};

struct probe_frame {
    adapted_probe probe;
    std::int32_t id = 0;

    auto operator==(const probe_frame&) const -> bool = default;
};

// The annotation guard must hold at every nesting depth: the annotated
// field rejects its own struct, and any struct containing that struct —
// probe_frame is trivially copyable and its reflected field walk sees only
// int32 cells, so without the recursive guard it would inline as a raw
// memcpy image and never run the adapter.
static_assert(!fbs::can_inline_struct_v<adapted_probe>);
static_assert(!fbs::can_inline_struct_v<probe_frame>);

ZEST_SUITE(codec_fbs_flatbuffers_behavior_attrs) {

ZEST_CASE(enum_string_roundtrip_on_struct_field) {
    const with_enum_string_field input{.id = 42, .level = role::editor};

    auto encoded = to_bytes(input);
    ASSERT(encoded);

    with_enum_string_field output{};
    auto status = from_bytes(*encoded, output);
    ASSERT(status);
    EXPECT(output == input);
}

ZEST_CASE(enum_string_roundtrip_viewer_value) {
    const with_enum_string_field input{.id = 7, .level = role::viewer};

    auto encoded = to_bytes(input);
    ASSERT(encoded);

    with_enum_string_field output{};
    auto status = from_bytes(*encoded, output);
    ASSERT(status);
    EXPECT(output == input);
}

ZEST_CASE(with_adapter_roundtrip_int_as_string) {
    const with_adapter_field input{.id = 9, .encoded = 12345, .tag = "gold"};

    auto encoded = to_bytes(input);
    ASSERT(encoded);

    with_adapter_field output{};
    auto status = from_bytes(*encoded, output);
    ASSERT(status);
    EXPECT(output == input);
}

ZEST_CASE(with_adapter_roundtrip_negative_value) {
    const with_adapter_field input{.id = 1, .encoded = -42, .tag = "debt"};

    auto encoded = to_bytes(input);
    ASSERT(encoded);

    with_adapter_field output{};
    auto status = from_bytes(*encoded, output);
    ASSERT(status);
    EXPECT(output == input);
}

ZEST_CASE(nested_annotated_aggregate_keeps_adapter) {
    struct holder {
        probe_frame frame;
    };

    holder input{};
    // The annotation inherits from calibration, so the field is set through
    // the base subobject.
    input.frame.probe.cal.code = 27;
    input.frame.id = 4;

    auto encoded = to_bytes(input);
    ASSERT(encoded);

    // The wire must hold the adapted string, not calibration's raw image:
    // frame and probe are tables, and the annotated slot is a string.
    auto root = fbs::table_view<holder>::from_bytes(
        std::span<const std::uint8_t>(encoded->data(), encoded->size()));
    ASSERT(root.valid());
    auto frame = root[&holder::frame];
    ASSERT(frame.valid());
    auto probe = frame[&probe_frame::probe];
    ASSERT(probe.valid());
    const std::string_view encoded_cal = probe[&adapted_probe::cal];
    EXPECT(encoded_cal == std::string_view{"27"});

    holder output{};
    ASSERT(from_bytes(*encoded, output).has_value());
    EXPECT(output.frame == input.frame);
}

ZEST_CASE(with_adapter_roundtrip_inside_optional, skip = true) {
    with_optional_adapter_field input{};
    input.maybe_encoded.emplace(7);

    auto encoded = to_bytes(input);
    ASSERT(encoded);

    with_optional_adapter_field output{};
    auto status = from_bytes(*encoded, output);
    ASSERT(status);
    ASSERT(output.maybe_encoded);
    EXPECT(annotated_value(*output.maybe_encoded) == 7);
}

ZEST_CASE(with_adapter_roundtrip_empty_optional) {
    const with_optional_adapter_field input{};

    auto encoded = to_bytes(input);
    ASSERT(encoded);

    with_optional_adapter_field output{};
    output.maybe_encoded.emplace(999);  // ensure decode clears it
    auto status = from_bytes(*encoded, output);
    ASSERT(status);
    EXPECT(!output.maybe_encoded);
}

};  // ZEST_SUITE(codec_fbs_flatbuffers_behavior_attrs)

}  // namespace

}  // namespace kota::codec

// ============================================================================
// Type-level traits tests: meta::repr
// ============================================================================

namespace kota_test_type_traits {

// A value-class wrapping an integer — not trivially reflectable, not an
// enum, and final (so kota's meta::annotation wrap_type path would apply).
// The adapter encodes it as a plain uint32_t.
class Tag final {
public:
    Tag() = default;

    explicit Tag(std::uint32_t v) : value_(v) {}

    auto value() const -> std::uint32_t {
        return value_;
    }

    auto operator==(const Tag&) const -> bool = default;
    auto operator<=>(const Tag&) const = default;

private:
    std::uint32_t value_ = 0;
};

// A value-class wrapping a byte sequence — emulates the roaring::Roaring
// shape (third-party class serialized as an opaque byte blob).
class ByteBag {
public:
    ByteBag() = default;

    explicit ByteBag(std::vector<std::byte> bytes) : bytes_(std::move(bytes)) {}

    auto bytes() const -> const std::vector<std::byte>& {
        return bytes_;
    }

    auto operator==(const ByteBag&) const -> bool = default;

private:
    std::vector<std::byte> bytes_;
};

// An integer id whose repr is imperative: the body drives the visitor, the
// declared string shape keeps the fbs layout honest.
class HexTag {
public:
    HexTag() = default;

    explicit HexTag(std::uint32_t v) : value_(v) {}

    auto value() const -> std::uint32_t {
        return value_;
    }

    auto operator==(const HexTag&) const -> bool = default;

private:
    std::uint32_t value_ = 0;
};

// An iterable value class (type_kind::array) with a scalar-string repr — the
// roaring-bitmap shape. Encode/decode of vector<IdSet> must agree that the
// element travels unwrapped as its repr string.
class IdSet {
public:
    IdSet() = default;

    explicit IdSet(std::vector<std::uint32_t> ids) : ids_(std::move(ids)) {}

    auto begin() const {
        return ids_.begin();
    }

    auto end() const {
        return ids_.end();
    }

    auto operator==(const IdSet&) const -> bool = default;

private:
    std::vector<std::uint32_t> ids_;
};

// A repr that must travel as a table: the string member rules out the
// inline-struct fast path, so vector elements need the table collectors.
struct EndpointRepr {
    std::string host;
    std::uint32_t port = 0;
};

class Endpoint {
public:
    Endpoint() = default;

    Endpoint(std::string host, std::uint32_t port) : host_(std::move(host)), port_(port) {}

    auto host() const -> const std::string& {
        return host_;
    }

    auto port() const -> std::uint32_t {
        return port_;
    }

    auto operator==(const Endpoint&) const -> bool = default;

private:
    std::string host_;
    std::uint32_t port_ = 0;
};

// A nullable repr: vector elements must be boxed like plain optionals.
class MaybeId {
public:
    MaybeId() = default;

    explicit MaybeId(std::optional<std::uint32_t> v) : value_(v) {}

    auto value() const -> std::optional<std::uint32_t> {
        return value_;
    }

    auto operator==(const MaybeId&) const -> bool = default;

private:
    std::optional<std::uint32_t> value_;
};

// A unit-like marker whose repr is null: elements carry no payload,
// but each must still occupy a vector entry.
class Marker {
public:
    auto operator==(const Marker&) const -> bool = default;
};

}  // namespace kota_test_type_traits

namespace kota::meta {

template <>
struct repr<kota_test_type_traits::Tag> {
    using type = std::uint32_t;

    static type to(const kota_test_type_traits::Tag& tag) {
        return tag.value();
    }

    static kota_test_type_traits::Tag from(type v) {
        return kota_test_type_traits::Tag{v};
    }
};

template <>
struct repr<kota_test_type_traits::ByteBag> {
    using type = std::vector<std::byte>;

    const static type& to(const kota_test_type_traits::ByteBag& bag) {
        return bag.bytes();
    }

    static kota_test_type_traits::ByteBag from(type bytes) {
        return kota_test_type_traits::ByteBag{std::move(bytes)};
    }
};

template <>
struct repr<kota_test_type_traits::HexTag> {
    using type = std::string;

    template <typename Config>
    static bool serialize(auto& vis, const kota_test_type_traits::HexTag& tag) {
        return vis.visit_str(std::to_string(tag.value()));
    }

    template <typename Config>
    static bool deserialize(auto& vis, kota_test_type_traits::HexTag& tag) {
        std::string encoded;
        if(!vis.visit_str(encoded))
            return false;
        tag = kota_test_type_traits::HexTag{static_cast<std::uint32_t>(std::stoul(encoded))};
        return true;
    }
};

template <>
struct repr<kota_test_type_traits::IdSet> {
    using type = std::string;

    static type to(const kota_test_type_traits::IdSet& set) {
        std::string encoded;
        for(auto id: set) {
            if(!encoded.empty())
                encoded += ',';
            encoded += std::to_string(id);
        }
        return encoded;
    }

    static kota_test_type_traits::IdSet from(const std::string& encoded) {
        std::vector<std::uint32_t> ids;
        std::size_t pos = 0;
        while(pos < encoded.size()) {
            auto comma = encoded.find(',', pos);
            if(comma == std::string::npos)
                comma = encoded.size();
            ids.push_back(static_cast<std::uint32_t>(std::stoul(encoded.substr(pos, comma - pos))));
            pos = comma + 1;
        }
        return kota_test_type_traits::IdSet{std::move(ids)};
    }
};

template <>
struct repr<kota_test_type_traits::Endpoint> {
    using type = kota_test_type_traits::EndpointRepr;

    static type to(const kota_test_type_traits::Endpoint& e) {
        return {.host = e.host(), .port = e.port()};
    }

    static kota_test_type_traits::Endpoint from(type encoded) {
        return {std::move(encoded.host), encoded.port};
    }
};

template <>
struct repr<kota_test_type_traits::MaybeId> {
    using type = std::optional<std::uint32_t>;

    static type to(const kota_test_type_traits::MaybeId& m) {
        return m.value();
    }

    static kota_test_type_traits::MaybeId from(type v) {
        return kota_test_type_traits::MaybeId{v};
    }
};

template <>
struct repr<kota_test_type_traits::Marker> {
    using type = std::nullptr_t;

    static type to(const kota_test_type_traits::Marker&) {
        return nullptr;
    }

    static kota_test_type_traits::Marker from(type) {
        return {};
    }
};

}  // namespace kota::meta

namespace kota::codec {

namespace {

using kota_test_type_traits::Tag;
using kota_test_type_traits::ByteBag;
using kota_test_type_traits::HexTag;
using kota_test_type_traits::IdSet;
using kota_test_type_traits::Endpoint;
using kota_test_type_traits::MaybeId;
using kota_test_type_traits::Marker;

struct TypeTraitsPlainField {
    Tag tag;
    std::string label;

    auto operator==(const TypeTraitsPlainField&) const -> bool = default;
};

struct TypeTraitsMapField {
    std::map<std::uint32_t, Tag> tags_by_id;
    std::map<std::uint32_t, ByteBag> blobs_by_id;

    auto operator==(const TypeTraitsMapField&) const -> bool = default;
};

struct TypeTraitsSequenceField {
    std::vector<Tag> tags;

    auto operator==(const TypeTraitsSequenceField&) const -> bool = default;
};

struct TypeTraitsRoot {
    Tag root_tag;
    std::map<std::uint32_t, ByteBag> blobs;
    std::string content;

    auto operator==(const TypeTraitsRoot&) const -> bool = default;
};

struct ImperativeReprField {
    HexTag tag;
    std::string label;

    auto operator==(const ImperativeReprField&) const -> bool = default;
};

struct IterableReprField {
    IdSet primary;
    std::vector<IdSet> groups;

    auto operator==(const IterableReprField&) const -> bool = default;
};

struct OptionalReprField {
    std::optional<Tag> maybe_tag;

    auto operator==(const OptionalReprField&) const -> bool = default;
};

struct TableReprField {
    std::vector<Endpoint> endpoints;

    auto operator==(const TableReprField&) const -> bool = default;
};

struct BoxedReprField {
    std::vector<MaybeId> ids;

    auto operator==(const BoxedReprField&) const -> bool = default;
};

struct BytesReprField {
    std::vector<ByteBag> blobs;

    auto operator==(const BytesReprField&) const -> bool = default;
};

struct NullReprField {
    std::vector<Marker> markers;
    std::string label;

    auto operator==(const NullReprField&) const -> bool = default;
};

// Adapter over a repr'd type: the field annotation must win over the type's
// own repr, in the encoded bytes and in the proxy view.
struct TagNameAdapter {
    using type = std::string;

    static auto to(const Tag& t) -> std::string {
        return std::to_string(t.value());
    }

    static auto from(const std::string& encoded) -> Tag {
        return Tag{static_cast<std::uint32_t>(std::stoul(encoded))};
    }
};

struct AdapterOverReprField {
    meta::annotation<Tag, meta::behavior::with<TagNameAdapter>> tag;
};

struct AdaptedElementField {
    std::vector<meta::annotation<int, meta::behavior::with<IntStringAdapter>>> vals;
};

ZEST_SUITE(codec_fbs_flatbuffers_behavior_attrs_type_traits) {

ZEST_CASE(type_traits_plain_field_roundtrip) {
    const TypeTraitsPlainField input{.tag = Tag{42}, .label = "hello"};

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    TypeTraitsPlainField output{};
    auto status = fbs::from_bytes(*encoded, output);
    ASSERT(status);
    EXPECT(output == input);
}

ZEST_CASE(type_traits_map_value_roundtrip) {
    TypeTraitsMapField input;
    input.tags_by_id[1] = Tag{100};
    input.tags_by_id[2] = Tag{200};
    input.blobs_by_id[10] = ByteBag{
        {std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}}
    };
    input.blobs_by_id[20] = ByteBag{{std::byte{0x11}}};

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    TypeTraitsMapField output{};
    auto status = fbs::from_bytes(*encoded, output);
    ASSERT(status);
    EXPECT(output == input);
}

ZEST_CASE(type_traits_sequence_element_roundtrip) {
    TypeTraitsSequenceField input;
    input.tags = {Tag{1}, Tag{2}, Tag{3}};

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    TypeTraitsSequenceField output{};
    auto status = fbs::from_bytes(*encoded, output);
    ASSERT(status);
    EXPECT(output == input);
}

ZEST_CASE(type_traits_proxy_lazy_scalar_access) {
    const TypeTraitsRoot input{.root_tag = Tag{777}, .blobs = {}, .content = "lazy"};
    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    auto root = fbs::table_view<TypeTraitsRoot>::from_bytes(
        std::span<const std::uint8_t>(encoded->data(), encoded->size()));
    ASSERT(root.valid());

    // Proxy sees the repr type (uint32_t)
    const std::uint32_t encoded_tag = root[&TypeTraitsRoot::root_tag];
    EXPECT(encoded_tag == 777U);

    const std::string_view content = root[&TypeTraitsRoot::content];
    EXPECT(content == std::string_view{"lazy"});
}

ZEST_CASE(map_with_repr_key_lookup) {
    // The ordering key resolves the key's repr first: Tag keys sort and look
    // up by their uint32 representation.
    struct tag_rank_map {
        std::map<Tag, std::int32_t> ranks;
    };

    tag_rank_map input;
    input.ranks[Tag{30}] = 3;
    input.ranks[Tag{7}] = 1;
    input.ranks[Tag{100}] = 5;

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    auto root = fbs::table_view<tag_rank_map>::from_bytes(
        std::span<const std::uint8_t>(encoded->data(), encoded->size()));
    ASSERT(root.valid());

    auto m = root[&tag_rank_map::ranks];
    ASSERT(m.valid());
    EXPECT(m[7U] == 1);
    EXPECT(m[30U] == 3);
    EXPECT(m[100U] == 5);
}

ZEST_CASE(type_traits_proxy_lazy_map_value_access) {
    TypeTraitsRoot input;
    input.root_tag = Tag{1};
    input.blobs[5] = ByteBag{
        {std::byte{0xDE}, std::byte{0xAD}}
    };
    input.blobs[9] = ByteBag{
        {std::byte{0xBE}, std::byte{0xEF}}
    };

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    auto root = fbs::table_view<TypeTraitsRoot>::from_bytes(
        std::span<const std::uint8_t>(encoded->data(), encoded->size()));
    ASSERT(root.valid());

    auto blobs = root[&TypeTraitsRoot::blobs];
    ASSERT(blobs.valid());
    EXPECT(blobs.size() == 2U);

    // map_view<K, ByteBag> — proxy substitutes the repr
    // (vector<byte>), so operator[] returns an array_view<std::byte>.
    auto blob5 = blobs[5U];
    ASSERT(blob5.valid());
    EXPECT(blob5.size() == 2U);
    EXPECT(blob5[0] == std::byte{0xDE});
    EXPECT(blob5[1] == std::byte{0xAD});

    auto blob9 = blobs[9U];
    ASSERT(blob9.valid());
    EXPECT(blob9.size() == 2U);
    EXPECT(blob9[0] == std::byte{0xBE});
    EXPECT(blob9[1] == std::byte{0xEF});
}

ZEST_CASE(imperative_repr_field_roundtrip) {
    const ImperativeReprField input{.tag = HexTag{54321}, .label = "imp"};

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    ImperativeReprField output{};
    auto status = fbs::from_bytes(*encoded, output);
    ASSERT(status);
    EXPECT(output == input);

    // The encoded table carries the declared string repr, observable via the proxy.
    auto root = fbs::table_view<ImperativeReprField>::from_bytes(
        std::span<const std::uint8_t>(encoded->data(), encoded->size()));
    ASSERT(root.valid());
    const std::string_view encoded_tag = root[&ImperativeReprField::tag];
    EXPECT(encoded_tag == std::string_view{"54321"});
}

ZEST_CASE(iterable_repr_element_roundtrip) {
    // IdSet's raw kind is array; its repr is a string. Encode and
    // decode of vector<IdSet> must agree the element is unwrapped.
    IterableReprField input;
    input.primary = IdSet{
        {1, 2, 3}
    };
    input.groups = {IdSet{{10, 20}}, IdSet{}, IdSet{{7}}};

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    IterableReprField output{};
    auto status = fbs::from_bytes(*encoded, output);
    ASSERT(status);
    EXPECT(output == input);
}

ZEST_CASE(repr_inside_optional_roundtrip) {
    OptionalReprField input{.maybe_tag = Tag{99}};

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    OptionalReprField output{};
    auto status = fbs::from_bytes(*encoded, output);
    ASSERT(status);
    EXPECT(output == input);

    input.maybe_tag.reset();
    encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    output.maybe_tag = Tag{1};
    status = fbs::from_bytes(*encoded, output);
    ASSERT(status);
    EXPECT(!output.maybe_tag);
}

ZEST_CASE(table_repr_element_roundtrip) {
    // Endpoint's repr is a table; vector elements must travel as table
    // offsets on both the encode and decode side.
    TableReprField input;
    input.endpoints = {
        Endpoint{"alpha", 1  },
        Endpoint{"",      0  },
        Endpoint{"beta",  443}
    };

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    TableReprField output{};
    auto status = fbs::from_bytes(*encoded, output);
    ASSERT(status);
    EXPECT(output == input);
}

ZEST_CASE(nullable_repr_element_roundtrip) {
    // MaybeId's repr is optional; vector elements must be boxed exactly
    // like plain optional elements.
    BoxedReprField input;
    input.ids = {MaybeId{7U}, MaybeId{}, MaybeId{42U}};

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    BoxedReprField output{};
    auto status = fbs::from_bytes(*encoded, output);
    ASSERT(status);
    EXPECT(output == input);
}

ZEST_CASE(view_reads_boxed_nullable_repr_elements) {
    // Boxed elements are read through their wrapper table; the view peels the
    // nullable repr to the inner scalar, absence reads as its default.
    BoxedReprField input;
    input.ids = {MaybeId{7U}, MaybeId{}, MaybeId{42U}};

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    auto root = fbs::table_view<BoxedReprField>::from_bytes(
        std::span<const std::uint8_t>(encoded->data(), encoded->size()));
    ASSERT(root.valid());

    auto ids = root[&BoxedReprField::ids];
    ASSERT(ids.valid());
    ASSERT(ids.size() == 3U);
    EXPECT(ids[0] == 7U);
    EXPECT(ids[1] == 0U);
    EXPECT(ids[2] == 42U);
}

ZEST_CASE(null_repr_element_roundtrip) {
    // Marker's repr is null-like; each element must still occupy a
    // vector entry (a per-element wrapper table), so the count round-trips.
    NullReprField input;
    input.markers = {Marker{}, Marker{}, Marker{}};
    input.label = "three";

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    NullReprField output{};
    auto status = fbs::from_bytes(*encoded, output);
    ASSERT(status);
    EXPECT(output.markers.size() == 3U);
    EXPECT(output == input);
}

ZEST_CASE(bytes_repr_element_roundtrip) {
    // ByteBag's repr is a byte blob; flatbuffers has no
    // vector-of-vectors, so elements travel boxed in per-element wrapper
    // tables, matching plain nested byte containers.
    BytesReprField input;
    input.blobs = {ByteBag{{std::byte{0xAA}, std::byte{0xBB}}},
                   ByteBag{},
                   ByteBag{{std::byte{0x01}}}};

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    BytesReprField output{};
    auto status = fbs::from_bytes(*encoded, output);
    ASSERT(status);
    EXPECT(output == input);

    auto root = fbs::table_view<BytesReprField>::from_bytes(
        std::span<const std::uint8_t>(encoded->data(), encoded->size()));
    ASSERT(root.valid());

    auto blobs = root[&BytesReprField::blobs];
    ASSERT(blobs.valid());
    ASSERT(blobs.size() == 3U);

    auto b0 = blobs[0];
    ASSERT(b0.valid());
    ASSERT(b0.size() == 2U);
    EXPECT(b0[0] == std::byte{0xAA});
    EXPECT(b0[1] == std::byte{0xBB});
    EXPECT(blobs[1].size() == 0U);
}

ZEST_CASE(adapted_element_travels_as_adapter_repr) {
    // The element's annotation adapter decides the repr (string), so
    // both sides must pick the string collectors, not the raw-int fast path.
    AdaptedElementField input;
    input.vals = {12, -3, 4567};

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    AdaptedElementField output{};
    auto status = fbs::from_bytes(*encoded, output);
    ASSERT(status);
    ASSERT(output.vals.size() == 3U);
    EXPECT(meta::annotated_value(output.vals[0]) == 12);
    EXPECT(meta::annotated_value(output.vals[1]) == -3);
    EXPECT(meta::annotated_value(output.vals[2]) == 4567);
}

ZEST_CASE(view_honors_field_adapter_over_type_repr) {
    // Tag's own repr is uint32, but the field adapter declares a string
    // representation; the proxy view must follow the adapter.
    const AdapterOverReprField input{.tag = Tag{4242}};

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    auto root = fbs::table_view<AdapterOverReprField>::from_bytes(
        std::span<const std::uint8_t>(encoded->data(), encoded->size()));
    ASSERT(root.valid());
    const std::string_view encoded_tag = root[&AdapterOverReprField::tag];
    EXPECT(encoded_tag == std::string_view{"4242"});

    AdapterOverReprField output{};
    ASSERT(fbs::from_bytes(*encoded, output).has_value());
    EXPECT(meta::annotated_value(output.tag) == Tag{4242});
}

};  // ZEST_SUITE(codec_fbs_flatbuffers_behavior_attrs_type_traits)

}  // namespace

}  // namespace kota::codec

// ============================================================================
// Format-scoped repr tests: meta::repr<T, codec::fbs::format>
// ============================================================================

namespace kota_test_format_scoped {

// A value class whose format-agnostic repr is textual, with a compact
// fbs-scoped override: flatbuffers must pick the uint32 form while every
// other backend keeps the string form.
class SensorId {
public:
    SensorId() = default;

    explicit SensorId(std::uint32_t v) : value_(v) {}

    auto value() const -> std::uint32_t {
        return value_;
    }

    auto operator==(const SensorId&) const -> bool = default;

private:
    std::uint32_t value_ = 0;
};

// An enum with an fbs-scoped repr, placed inside an otherwise trivial
// struct: the repr replaces the raw layout, so the struct must not be
// classified as an inline (memcpy) struct.
enum class probe_kind : std::uint8_t {
    heat,
    light,
};

// No default member initializers: CellProbe must stay trivial so that only
// the repr on probe_kind disqualifies the inline classification.
struct CellProbe {
    probe_kind kind;
    float reading;

    auto operator==(const CellProbe&) const -> bool = default;
};

}  // namespace kota_test_format_scoped

namespace kota::meta {

template <>
struct repr<kota_test_format_scoped::SensorId> {
    using type = std::string;

    static type to(const kota_test_format_scoped::SensorId& id) {
        return "s" + std::to_string(id.value());
    }

    static kota_test_format_scoped::SensorId from(const std::string& encoded) {
        return kota_test_format_scoped::SensorId{
            static_cast<std::uint32_t>(std::stoul(encoded.substr(1)))};
    }
};

template <>
struct repr<kota_test_format_scoped::SensorId, codec::fbs::format> {
    using type = std::uint32_t;

    static type to(const kota_test_format_scoped::SensorId& id) {
        return id.value();
    }

    static kota_test_format_scoped::SensorId from(type v) {
        return kota_test_format_scoped::SensorId{v};
    }
};

template <>
struct repr<kota_test_format_scoped::probe_kind, codec::fbs::format> {
    using type = std::uint32_t;

    static type to(kota_test_format_scoped::probe_kind k) {
        return static_cast<type>(k);
    }

    static kota_test_format_scoped::probe_kind from(type v) {
        return static_cast<kota_test_format_scoped::probe_kind>(v);
    }
};

}  // namespace kota::meta

namespace kota::codec {

namespace {

using kota_test_format_scoped::CellProbe;
using kota_test_format_scoped::probe_kind;
using kota_test_format_scoped::SensorId;

struct SensorReading {
    SensorId id;
    std::string label;

    auto operator==(const SensorReading&) const -> bool = default;
};

struct ProbeGrid {
    std::vector<CellProbe> cells;

    auto operator==(const ProbeGrid&) const -> bool = default;
};

struct SensorList {
    std::vector<SensorId> ids;

    auto operator==(const SensorList&) const -> bool = default;
};

// fbs resolves the format-scoped repr; the bare (format-agnostic) view keeps
// the string form other backends dispatch on.
static_assert(std::is_same_v<meta::resolved_repr_t<SensorId, fbs::format>, std::uint32_t>);
static_assert(std::is_same_v<meta::resolved_repr_t<SensorId>, std::string>);

// The repr'd enum disqualifies the memcpy image; the struct degrades to a
// table where the dispatch applies the repr.
static_assert(!fbs::can_inline_struct_v<CellProbe>);

ZEST_SUITE(codec_fbs_flatbuffers_behavior_attrs_format_scoped) {

ZEST_CASE(format_scoped_repr_selected_by_fbs) {
    const SensorReading input{.id = SensorId{7}, .label = "porch"};

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    // The table slot holds the fbs-scoped uint32, not the string form.
    auto root = fbs::table_view<SensorReading>::from_bytes(
        std::span<const std::uint8_t>(encoded->data(), encoded->size()));
    ASSERT(root.valid());
    const std::uint32_t raw = root[&SensorReading::id];
    EXPECT(raw == 7U);

    SensorReading output{};
    ASSERT(fbs::from_bytes(*encoded, output).has_value());
    EXPECT(output == input);
}

ZEST_CASE(other_backends_keep_format_agnostic_repr) {
    const SensorReading input{.id = SensorId{7}, .label = "porch"};

    auto encoded = json::to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"id":"s7","label":"porch"})");

    SensorReading output{};
    ASSERT(json::from_string(*encoded, output).has_value());
    EXPECT(output == input);
}

ZEST_CASE(format_scoped_repr_reaches_vector_elements) {
    // Element classification (element_layout_of / scalar cells) must resolve
    // the same fbs-scoped repr as the dispatch: the vector stores uint32
    // scalars, not string offsets.
    const SensorList input{
        .ids = {SensorId{3}, SensorId{9}}
    };

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    auto root = fbs::table_view<SensorList>::from_bytes(
        std::span<const std::uint8_t>(encoded->data(), encoded->size()));
    ASSERT(root.valid());

    auto ids = root[&SensorList::ids];
    ASSERT(ids.valid());
    ASSERT(ids.size() == 2U);
    EXPECT(ids[0] == 3U);
    EXPECT(ids[1] == 9U);

    SensorList output{};
    ASSERT(fbs::from_bytes(*encoded, output).has_value());
    EXPECT(output == input);
}

ZEST_CASE(repr_field_blocks_inline_struct) {
    const ProbeGrid input{
        .cells = {{.kind = probe_kind::light, .reading = 1.5F},
                  {.kind = probe_kind::heat, .reading = -2.0F}},
    };

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    ProbeGrid output{};
    ASSERT(fbs::from_bytes(*encoded, output).has_value());
    EXPECT(output == input);
}

};  // ZEST_SUITE(codec_fbs_flatbuffers_behavior_attrs_format_scoped)

}  // namespace

}  // namespace kota::codec

#endif
