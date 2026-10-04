#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "codec/fbs/harness/struct_keys.h"
#include "codec/harness/fixtures/attrs.h"
#include "codec/harness/fixtures/configs.h"
#include "codec/harness/fixtures/enums.h"
#include "codec/harness/fixtures/repr.h"
#include "codec/harness/fixtures/structs.h"
#include "fixtures/repr.h"
#include "kota/zest/zest.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"
#include "kota/codec/fbs/fbs.h"

// What the fbs encoder writes: which structs inline and which become tables,
// the slot layout of tables, tuples, variants, vectors and maps, and the
// guarantees on the bytes (zeroed padding, sorted map entries). Buffers are
// read with flatbuffers' own accessors; the zero-copy views are
// proxy_tests.cpp's.

namespace kota::codec {

namespace {

/// A class the fbs backend carries as a byte blob.
class ByteBag {
public:
    ByteBag() = default;

    explicit ByteBag(std::vector<std::byte> bytes) : bytes(std::move(bytes)) {}

    auto data() const -> const std::vector<std::byte>& {
        return bytes;
    }

    auto operator==(const ByteBag&) const -> bool = default;

private:
    std::vector<std::byte> bytes;
};

/// An iterable class (type_kind::array) carried as a comma-separated string.
class IdSet {
public:
    IdSet() = default;

    explicit IdSet(std::vector<std::uint32_t> ids) : ids(std::move(ids)) {}

    auto begin() const {
        return ids.begin();
    }

    auto end() const {
        return ids.end();
    }

    auto operator==(const IdSet&) const -> bool = default;

private:
    std::vector<std::uint32_t> ids;
};

/// A class carried as a table: the string keeps its repr from inlining.
struct EndpointRecord {
    std::string host;
    std::uint32_t port = 0;
};

class Endpoint {
public:
    Endpoint() = default;

    Endpoint(std::string host, std::uint32_t port) : host(std::move(host)), port(port) {}

    auto record() const -> EndpointRecord {
        return {.host = host, .port = port};
    }

    auto operator==(const Endpoint&) const -> bool = default;

private:
    std::string host;
    std::uint32_t port = 0;
};

/// A unit-like class carried as null.
struct Marker {
    auto operator==(const Marker&) const -> bool = default;
};

/// A scoped enum with an fbs-scoped repr: the repr, not the enum's bytes,
/// is what travels.
enum class ProbeKind : std::uint8_t { heat, light };

/// Aggregate whose annotation derives from it: reflection walks straight
/// through the annotation, so only the annotation guard keeps the adapter.
struct Calibration {
    std::int32_t code = 0;

    auto operator==(const Calibration&) const -> bool = default;
};

struct CalibrationText {
    using type = std::string;

    static std::string to(const Calibration& c) {
        return std::to_string(c.code);
    }

    static Calibration from(const std::string& text) {
        return {.code = std::stoi(text)};
    }
};

}  // namespace

}  // namespace kota::codec

namespace kota::meta {

template <>
struct repr<codec::ByteBag> {
    using type = std::vector<std::byte>;

    const static type& to(const codec::ByteBag& bag) {
        return bag.data();
    }

    static codec::ByteBag from(type bytes) {
        return codec::ByteBag{std::move(bytes)};
    }
};

template <>
struct repr<codec::IdSet> {
    using type = std::string;

    static type to(const codec::IdSet& set) {
        std::string text;
        for(auto id: set) {
            if(!text.empty()) {
                text += ',';
            }
            text += std::to_string(id);
        }
        return text;
    }

    static codec::IdSet from(const std::string& text) {
        std::vector<std::uint32_t> ids;
        std::size_t pos = 0;
        while(pos < text.size()) {
            auto comma = std::min(text.find(',', pos), text.size());
            ids.push_back(static_cast<std::uint32_t>(std::stoul(text.substr(pos, comma - pos))));
            pos = comma + 1;
        }
        return codec::IdSet{std::move(ids)};
    }
};

template <>
struct repr<codec::Endpoint> {
    using type = codec::EndpointRecord;

    static type to(const codec::Endpoint& e) {
        return e.record();
    }

    static codec::Endpoint from(type record) {
        return {std::move(record.host), record.port};
    }
};

template <>
struct repr<codec::Marker> {
    using type = std::nullptr_t;

    static type to(const codec::Marker&) {
        return nullptr;
    }

    static codec::Marker from(type) {
        return {};
    }
};

template <>
struct repr<codec::ProbeKind, codec::fbs::format> {
    using type = std::uint32_t;

    static type to(codec::ProbeKind k) {
        return static_cast<type>(k);
    }

    static codec::ProbeKind from(type v) {
        return static_cast<codec::ProbeKind>(v);
    }
};

}  // namespace kota::meta

namespace kota::codec {

namespace {

using Buffer = std::vector<std::uint8_t>;
using EntryVector = fbs::Vector<fbs::offset_t<fbs::Table>>;

/// The voffset of a table's field `index`: type.h puts them at 4, 6, 8, ...
/// in declaration order.
constexpr fbs::voffset_t slot(std::size_t index) {
    return static_cast<fbs::voffset_t>(4 + 2 * index);
}

auto root_of(const Buffer& bytes) -> const fbs::Table* {
    return ::flatbuffers::GetRoot<fbs::Table>(bytes.data());
}

/// The key of entry `index` of the map stored at `field` of `table`; nothing
/// when there is no such map or entry.
template <typename Key>
auto entry_key(const fbs::Table* table, std::size_t field, std::size_t index)
    -> std::optional<Key> {
    const auto* entries = table->GetPointer<const EntryVector*>(slot(field));
    if(entries == nullptr || index >= entries->size()) {
        return std::nullopt;
    }
    return entries->Get(static_cast<fbs::uoffset_t>(index))->GetField<Key>(slot(0), Key{});
}

struct Frame {
    std::int32_t id;
    test::Point pos;
    test::Address addr;
};

struct Optionals {
    std::optional<std::int32_t> number;
    std::unique_ptr<test::Point> owned;
    std::optional<test::Address> addr;
};

struct NullPayload {
    std::optional<std::monostate> none;
    std::int32_t tail{};
};

struct Route {
    std::vector<test::Point> points;
    std::list<test::Point> waypoints;

    auto operator==(const Route&) const -> bool = default;
};

/// char and std::byte are scalars to the fbs backend, so they inline.
struct Probe {
    char tag;
    std::byte flags;
    std::int32_t count;

    auto operator==(const Probe&) const -> bool = default;
};

/// Everything a struct may hold that keeps it from inlining, each beside a
/// plain int so only that member decides.
struct WithOptional {
    std::optional<std::int32_t> cached;
    std::int32_t id = 0;

    auto operator==(const WithOptional&) const -> bool = default;
};

struct WithLongDouble {
    long double ratio = 0.0L;
    std::int32_t id = 0;

    auto operator==(const WithLongDouble&) const -> bool = default;
};

enum DeducedMode { mode_off, mode_on };

struct WithDeducedEnum {
    DeducedMode mode = mode_off;
    std::int32_t id = 0;

    auto operator==(const WithDeducedEnum&) const -> bool = default;
};

enum FixedMode : std::uint8_t { zone_red, zone_blue };

struct WithFixedEnum {
    FixedMode zone = zone_red;
    std::int32_t id = 0;

    auto operator==(const WithFixedEnum&) const -> bool = default;
};

struct CellProbe {
    ProbeKind kind;
    float reading;

    auto operator==(const CellProbe&) const -> bool = default;
};

struct AdaptedProbe {
    meta::annotation<Calibration, meta::behavior::with<CalibrationText>> cal;

    auto operator==(const AdaptedProbe&) const -> bool = default;
};

/// Holds the adapted struct one level down: its own fields are all int32
/// cells to reflection.
struct ProbeFrame {
    AdaptedProbe probe;
    std::int32_t id = 0;

    auto operator==(const ProbeFrame&) const -> bool = default;
};

/// Trivially copyable, yet its copy assignment is deleted: decode restores
/// an inline struct by assignment, so this one must be a table.
struct Pinned {
    char tag = 0;
    std::int32_t id = 0;

    Pinned& operator=(const Pinned&) = delete;

    friend bool operator==(const Pinned&, const Pinned&) = default;
};

/// One past the reflection field limit: meta::field_count() reads zero.
struct OverLimit {
    std::int32_t f00, f01, f02, f03, f04, f05, f06, f07, f08, f09, f10, f11, f12, f13, f14, f15,
        f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30, f31, f32, f33,
        f34, f35, f36, f37, f38, f39, f40, f41, f42, f43, f44, f45, f46, f47, f48, f49, f50, f51,
        f52, f53, f54, f55, f56, f57, f58, f59, f60, f61, f62, f63, f64, f65, f66, f67, f68, f69,
        f70, f71, f72;
};

struct HoldsOverLimit {
    OverLimit wide;
};

/// A struct in a vector and alone, so both inline paths are taken.
template <typename T>
struct Placed {
    std::vector<T> entries;
    T solo;
};

/// The solo field of a Placed<T> is stored as a table rather than inline.
bool solo_is_a_table(const Buffer& bytes) {
    return root_of(bytes)->GetPointer<const fbs::Table*>(slot(1)) != nullptr;
}

/// char followed by int32: three padding bytes.
struct Padded {
    char tag = 0;
    std::int32_t id = 0;

    friend bool operator==(const Padded&, const Padded&) = default;
};

struct PaddedLess {
    bool operator()(const Padded& a, const Padded& b) const {
        return std::tie(a.tag, a.id) < std::tie(b.tag, b.id);
    }
};

struct WithPadded {
    std::vector<Padded> items;
    Padded solo;
    std::map<Padded, std::int32_t, PaddedLess> scores;
};

/// A Padded whose padding holds `fill`. A copy may drop the fill, so the
/// stored objects are scribbled again in place with scribble().
auto scribbled(std::uint8_t fill, char tag, std::int32_t id) -> Padded {
    std::array<std::byte, sizeof(Padded)> bytes;
    bytes.fill(std::byte{fill});
    Padded padded;
    std::memcpy(&padded, bytes.data(), sizeof(padded));
    padded.tag = tag;
    padded.id = id;
    return padded;
}

/// Writes `fill` into a stored Padded's padding, in its final storage, so no
/// later copy can drop it on the way to the encoder.
void scribble(Padded& stored, std::uint8_t fill) {
    const Padded value = stored;
    std::array<std::byte, sizeof(Padded)> bytes;
    bytes.fill(std::byte{fill});
    std::memcpy(bytes.data() + offsetof(Padded, tag), &value.tag, sizeof(value.tag));
    std::memcpy(bytes.data() + offsetof(Padded, id), &value.id, sizeof(value.id));
    std::memcpy(&stored, bytes.data(), sizeof(stored));
}

bool padding_is_zero(const Padded* stored) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(stored);
    for(std::size_t i = sizeof(char); i < offsetof(Padded, id); ++i) {
        if(bytes[i] != 0) {
            return false;
        }
    }
    return true;
}

using test::OccurrenceKey;
using test::SentinelRange;

/// The reverse of the reflected field order: the encoder must sort by its
/// own ordering, not trust the container's.
struct ReverseKeyLess {
    bool operator()(const OccurrenceKey& a, const OccurrenceKey& b) const {
        return std::tie(b.range.begin, b.range.end, b.target, b.weight) <
               std::tie(a.range.begin, a.range.end, a.target, a.weight);
    }
};

struct OccurrenceKeyHash {
    std::size_t operator()(const OccurrenceKey& k) const {
        return std::hash<std::uint64_t>{}(k.target) ^ (k.range.begin * 31U) ^ k.range.end ^
               static_cast<std::uint32_t>(k.weight);
    }
};

/// A string keeps it a table, whose fields the protocol visits one by one.
struct LabelledReading {
    double value = 0;
    std::string unit;
};

using NumberedReading = std::tuple<int, LabelledReading>;

struct ReadingLog {
    std::vector<LabelledReading> readings;
    std::map<std::string, LabelledReading> by_name;
    NumberedReading numbered;
    std::vector<NumberedReading> numbered_list;
};

/// Inline: its doubles travel as the struct's image.
struct Reading {
    double value;
    double error;
};

ZEST_SUITE(codec_fbs_encode) {

ZEST_CASE(buffer_carries_the_identifier) {
    auto bytes = fbs::to_bytes(test::Point{.x = 1, .y = 2});
    ZASSERT(bytes);
    ZEXPECT(::flatbuffers::BufferHasIdentifier(bytes->data(), "EVTO"));
}

ZEST_CASE(trivially_copyable_struct_field_is_inline) {
    ZSTATIC_EXPECT(fbs::can_inline_struct_v<test::Point>);
    ZSTATIC_EXPECT(!fbs::can_inline_struct_v<test::Address>);
    auto bytes = fbs::to_bytes(Frame{
        .id = 7,
        .pos = {.x = 10,      .y = 20      },
        .addr = {.city = "sh", .zip = 200000}
    });
    ZASSERT(bytes);
    const auto* root = root_of(*bytes);
    ZEXPECT(root->GetField<std::int32_t>(slot(0), 0) == 7);
    const auto* pos = root->GetStruct<const test::Point*>(slot(1));
    ZASSERT(pos != nullptr);
    ZEXPECT(pos->x == 10);
    ZEXPECT(pos->y == 20);
    const auto* addr = root->GetPointer<const fbs::Table*>(slot(2));
    ZASSERT(addr != nullptr);
    ZEXPECT(addr->GetField<std::int32_t>(slot(1), 0) == 200000);
}

ZEST_CASE(char_and_byte_fields_keep_a_struct_inline) {
    ZSTATIC_EXPECT(fbs::can_inline_struct_v<Probe>);
    const Placed<Probe> input{
        .entries = {{.tag = 'k', .flags = std::byte{0x5A}, .count = 3}},
        .solo = {.tag = 'q', .flags = std::byte{0xA5}, .count = -1},
    };
    auto bytes = fbs::to_bytes(input);
    ZASSERT(bytes);
    const auto* solo = root_of(*bytes)->GetStruct<const Probe*>(slot(1));
    ZASSERT(solo != nullptr);
    ZEXPECT(solo->flags == std::byte{0xA5});
    auto decoded = fbs::from_bytes<Placed<Probe>>(*bytes);
    ZASSERT(decoded);
    ZEXPECT(decoded->entries == input.entries);
    ZEXPECT(decoded->solo == input.solo);
}

ZEST_CASE(inline_structs_in_a_vector_are_a_struct_vector) {
    // A contiguous vector is copied whole; a list goes element by element.
    const Route input{
        .points = {{.x = 1, .y = 2}, {.x = 3, .y = 4}},
        .waypoints = {{.x = 5, .y = 6}, {.x = 7, .y = 8}},
    };
    auto bytes = fbs::to_bytes(input);
    ZASSERT(bytes);
    const auto* root = root_of(*bytes);
    for(std::size_t field: {0U, 1U}) {
        ZEST_CONTEXT("field {}", field);
        const auto* points = root->GetPointer<const fbs::Vector<const test::Point*>*>(slot(field));
        ZASSERT(points != nullptr);
        ZASSERT(points->size() == 2U);
        ZEXPECT(points->Get(1)->y == (field == 0 ? 4 : 8));
    }
    auto decoded = fbs::from_bytes<Route>(*bytes);
    ZASSERT(decoded);
    ZEXPECT(*decoded == input);
}

ZEST_CASE(skipped_field_takes_no_slot) {
    // Slots follow the schema, which leaves the skipped field out; the views
    // map members to slots through the same schema.
    auto bytes = fbs::to_bytes(test::FlattenInnerWithSkip{.keep_a = 3, .drop_b = 999, .keep_c = 5});
    ZASSERT(bytes);
    const auto* root = root_of(*bytes);
    ZEXPECT(root->GetField<std::int32_t>(slot(0), 0) == 3);
    ZEXPECT(root->GetField<std::int32_t>(slot(1), 0) == 5);
    ZEXPECT(root->GetOptionalFieldOffset(slot(2)) == 0U);
}

ZEST_CASE(engaged_optional_of_a_null_reads_back_disengaged) {
    // A null payload leaves its slot absent, as a disengaged optional does,
    // so nothing tells the two apart.
    auto bytes = fbs::to_bytes(NullPayload{.none = std::monostate{}, .tail = 7});
    ZASSERT(bytes);
    ZEXPECT(root_of(*bytes)->GetOptionalFieldOffset(slot(0)) == 0U);
    auto decoded = fbs::from_bytes<NullPayload>(*bytes);
    ZASSERT(decoded);
    ZEXPECT(!decoded->none);
    ZEXPECT(decoded->tail == 7);
}

ZEST_CASE(absent_slot_reads_as_null) {
    // A slot decode visits every slot, and an absent one is how a null
    // travels: whatever the target held, the field reads as null, empty or
    // zero. Skippable's initializers are none of those.
    auto bytes = fbs::to_bytes(test::IdOnly{.id = 1});
    ZASSERT(bytes);
    test::Skippable decoded{};
    ZASSERT(fbs::from_bytes(*bytes, decoded));
    ZEXPECT(meta::eq(
        decoded,
        test::Skippable{.id = 1, .note = std::nullopt, .tags = {}, .generation = 0, .score = 0}));
}

ZEST_CASE(empty_nullable_leaves_its_slot_absent) {
    auto empty = fbs::to_bytes(Optionals{});
    ZASSERT(empty);
    for(std::size_t field: {0U, 1U, 2U}) {
        ZEST_CONTEXT("field {}", field);
        ZEXPECT(root_of(*empty)->GetOptionalFieldOffset(slot(field)) == 0U);
    }

    auto engaged = fbs::to_bytes(Optionals{
        .number = 0,
        .owned = std::make_unique<test::Point>(),
        .addr = test::Address{},
    });
    ZASSERT(engaged);
    for(std::size_t field: {0U, 1U, 2U}) {
        ZEST_CONTEXT("field {}", field);
        ZEXPECT(root_of(*engaged)->GetOptionalFieldOffset(slot(field)) != 0U);
    }
}

ZEST_CASE(non_table_root_is_boxed_at_the_first_slot) {
    auto number = fbs::to_bytes(std::int32_t{42});
    ZASSERT(number);
    ZEXPECT(root_of(*number)->GetField<std::int32_t>(slot(0), 0) == 42);

    auto list = fbs::to_bytes(std::vector<std::int32_t>{3, 5, 8});
    ZASSERT(list);
    const auto* vec = root_of(*list)->GetPointer<const fbs::Vector<std::int32_t>*>(slot(0));
    ZASSERT(vec != nullptr);
    ZASSERT(vec->size() == 3U);
    ZEXPECT(vec->Get(2) == 8);

    auto none = fbs::to_bytes(std::optional<std::int32_t>{});
    ZASSERT(none);
    ZEXPECT(root_of(*none)->GetOptionalFieldOffset(slot(0)) == 0U);
}

ZEST_CASE(tuple_is_a_table_of_slots) {
    // std::array is tuple-like, so it too is a table, not a vector.
    using Row = std::tuple<std::int32_t, std::string, std::array<std::int32_t, 3>>;
    auto bytes = fbs::to_bytes(Row{
        7,
        "seven",
        {1, 3, 5}
    });
    ZASSERT(bytes);
    const auto* root = root_of(*bytes);
    ZEXPECT(root->GetField<std::int32_t>(slot(0), 0) == 7);
    const auto* text = root->GetPointer<const fbs::String*>(slot(1));
    ZASSERT(text != nullptr);
    ZEXPECT(text->str() == "seven");
    const auto* triple = root->GetPointer<const fbs::Table*>(slot(2));
    ZASSERT(triple != nullptr);
    ZEXPECT(triple->GetField<std::int32_t>(slot(2), 0) == 5);
}

ZEST_CASE(variant_is_a_tag_and_a_payload_slot) {
    // The tag sits at the first slot, the payload at the slot after the
    // alternative's index; a container payload is stored as itself.
    using Choice = std::variant<std::monostate, std::vector<std::int32_t>, test::Point>;
    auto list = fbs::to_bytes(Choice{
        std::vector<std::int32_t>{1, 2, 3}
    });
    ZASSERT(list);
    const auto* root = root_of(*list);
    ZEXPECT(root->GetField<std::uint32_t>(slot(0), 99) == 1U);
    const auto* vec = root->GetPointer<const fbs::Vector<std::int32_t>*>(slot(2));
    ZASSERT(vec != nullptr);
    ZEXPECT(vec->size() == 3U);

    auto point = fbs::to_bytes(Choice{
        test::Point{.x = 1, .y = 2}
    });
    ZASSERT(point);
    const auto* stored = root_of(*point)->GetStruct<const test::Point*>(slot(3));
    ZASSERT(stored != nullptr);
    ZEXPECT(stored->y == 2);

    // A monostate payload is a null: its slot is absent.
    auto none = fbs::to_bytes(Choice{});
    ZASSERT(none);
    ZEXPECT(root_of(*none)->GetField<std::uint32_t>(slot(0), 99) == 0U);
    ZEXPECT(root_of(*none)->GetOptionalFieldOffset(slot(1)) == 0U);
}

ZEST_CASE(nullable_and_container_elements_are_boxed) {
    // A vector entry cannot be absent, and flatbuffers has no vector of
    // vectors, so each such element sits in a table of its own.
    struct Boxes {
        std::vector<std::optional<std::int32_t>> maybe;
        std::vector<std::vector<std::byte>> blobs;
    };

    auto bytes = fbs::to_bytes(Boxes{
        .maybe = {5,                 std::nullopt},
        .blobs = {{std::byte{0xAA}}, {}          }
    });
    ZASSERT(bytes);
    const auto* maybe = root_of(*bytes)->GetPointer<const EntryVector*>(slot(0));
    ZASSERT(maybe != nullptr);
    ZASSERT(maybe->size() == 2U);
    ZEXPECT(maybe->Get(0)->GetField<std::int32_t>(slot(0), 0) == 5);
    ZEXPECT(maybe->Get(1)->GetOptionalFieldOffset(slot(0)) == 0U);
    const auto* blobs = root_of(*bytes)->GetPointer<const EntryVector*>(slot(1));
    ZASSERT(blobs != nullptr);
    ZASSERT(blobs->size() == 2U);
    const auto* first = blobs->Get(0)->GetPointer<const fbs::Vector<std::uint8_t>*>(slot(0));
    ZASSERT(first != nullptr);
    ZASSERT(first->size() == 1U);
    ZEXPECT(first->Get(0) == 0xAAU);
}

ZEST_CASE(map_entries_sort_by_their_key) {
    // Numbers sort as numbers (2 before 10), unsigned keys as unsigned, and
    // enums by their underlying value, whatever the container's order.
    struct Keyed {
        std::map<std::int32_t, std::string> by_id;
        std::unordered_map<std::uint64_t, std::int32_t> by_wide_id;
        std::unordered_map<test::SignedEnum, std::int32_t> by_sign;
    };

    const Keyed input{
        .by_id = {{10, "ten"},                {-7, "minus"}, {2, "two"}},
        .by_wide_id = {{0x8000000000000001ULL, 2}, {2U, 1},       {42U, 3}  },
        .by_sign = {{test::SignedEnum::pos, 3},
                  {test::SignedEnum::neg, 1},
                  {test::SignedEnum::zero, 2}                          },
    };
    auto bytes = fbs::to_bytes(input);
    ZASSERT(bytes);
    const auto* root = root_of(*bytes);
    ZEXPECT(entry_key<std::int32_t>(root, 0, 0) == -7);
    ZEXPECT(entry_key<std::int32_t>(root, 0, 1) == 2);
    ZEXPECT(entry_key<std::int32_t>(root, 0, 2) == 10);
    ZEXPECT(entry_key<std::uint64_t>(root, 1, 0) == 2U);
    ZEXPECT(entry_key<std::uint64_t>(root, 1, 1) == 42U);
    ZEXPECT(entry_key<std::uint64_t>(root, 1, 2) == 0x8000000000000001ULL);
    ZEXPECT(entry_key<std::int32_t>(root, 2, 0) == -42);
    ZEXPECT(entry_key<std::int32_t>(root, 2, 2) == 42);
}

ZEST_CASE(struct_keyed_map_entries_sort_field_by_field) {
    // Each adjacent pair is decided by a different field against a contrary
    // later one: k1/k2 by the signed weight, k2/k3 by target, k3/k4 by the
    // nested range.end, k4/k5 by range.begin.
    const std::array<OccurrenceKey, 5> keys{
        {
         {.range = {.begin = 1, .end = 5}, .target = 9, .weight = -2},
         {.range = {.begin = 1, .end = 5}, .target = 9, .weight = 3},
         {.range = {.begin = 1, .end = 5}, .target = 10, .weight = -9},
         {.range = {.begin = 1, .end = 6}, .target = 0, .weight = 0},
         {.range = {.begin = 2, .end = 0}, .target = 0, .weight = 0},
         }
    };

    struct Ordered {
        std::map<OccurrenceKey, std::int32_t, ReverseKeyLess> data;
    };

    struct Hashed {
        std::unordered_map<OccurrenceKey, std::int32_t, OccurrenceKeyHash> data;
    };

    Ordered ordered;
    Hashed hashed;
    for(std::size_t i = 0; i < keys.size(); ++i) {
        ordered.data.emplace(keys[i], static_cast<std::int32_t>(i));
        hashed.data.emplace(keys[i], static_cast<std::int32_t>(i));
    }
    auto ordered_bytes = fbs::to_bytes(ordered);
    ZASSERT(ordered_bytes);
    auto hashed_bytes = fbs::to_bytes(hashed);
    ZASSERT(hashed_bytes);

    // The entry vector is sorted; the entry tables themselves sit wherever
    // the builder placed them, in the container's order.
    for(const Buffer* bytes: {&*ordered_bytes, &*hashed_bytes}) {
        const auto* entries = root_of(*bytes)->GetPointer<const EntryVector*>(slot(0));
        ZASSERT(entries != nullptr);
        ZASSERT(entries->size() == keys.size());
        for(std::size_t i = 0; i < keys.size(); ++i) {
            ZEST_CONTEXT("entry {}", i);
            const auto* key = entries->Get(static_cast<fbs::uoffset_t>(i))
                                  ->GetStruct<const OccurrenceKey*>(slot(0));
            ZASSERT(key != nullptr);
            ZEXPECT(*key == keys[i]);
        }
    }

    auto decoded = fbs::from_bytes<Ordered>(*ordered_bytes);
    ZASSERT(decoded);
    ZEXPECT(meta::eq(decoded->data, ordered.data));
}

ZEST_CASE(long_double_is_a_double_cell) {
    // Its native image is ABI-specific and can hold unspecified bytes.
    struct Samples {
        std::vector<long double> values;
        long double scale = 1.0L;
    };

    const Samples input{
        .values = {1.5L, -2.25L},
        .scale = 3.5L
    };
    auto bytes = fbs::to_bytes(input);
    ZASSERT(bytes);
    const auto* root = root_of(*bytes);
    const auto* values = root->GetPointer<const fbs::Vector<double>*>(slot(0));
    ZASSERT(values != nullptr);
    ZASSERT(values->size() == 2U);
    ZEXPECT(values->Get(1) == -2.25);
    ZEXPECT(root->GetField<double>(slot(1), 0) == 3.5);
    auto decoded = fbs::from_bytes<Samples>(*bytes);
    ZASSERT(decoded);
    ZEXPECT(decoded->values == input.values);
    ZEXPECT(decoded->scale == input.scale);
}

ZEST_CASE(members_that_keep_a_struct_a_table) {
    // An optional's flag, a long double's image and a deduced enum's value
    // are not valid for every byte pattern, a repr or an annotation replaces
    // the raw member, and a struct past the reflection limit shows no
    // fields: none can be a memcpy image. An explicit enum base can.
    ZSTATIC_EXPECT(std::is_trivially_copyable_v<WithOptional>);
    ZSTATIC_EXPECT(!fbs::can_inline_struct_v<WithOptional>);
    ZSTATIC_EXPECT(std::is_trivially_copyable_v<WithLongDouble>);
    ZSTATIC_EXPECT(!fbs::can_inline_struct_v<WithLongDouble>);
    ZSTATIC_EXPECT(!fbs::can_inline_struct_v<WithDeducedEnum>);
    ZSTATIC_EXPECT(fbs::can_inline_struct_v<WithFixedEnum>);
    ZSTATIC_EXPECT(!fbs::can_inline_struct_v<CellProbe>);
    ZSTATIC_EXPECT(!fbs::can_inline_struct_v<AdaptedProbe>);
    ZSTATIC_EXPECT(!fbs::can_inline_struct_v<ProbeFrame>);
    ZSTATIC_EXPECT(meta::field_count<OverLimit>() == 0U);
    ZSTATIC_EXPECT(!fbs::can_inline_struct_v<OverLimit>);
    ZSTATIC_EXPECT(!fbs::can_inline_struct_v<HoldsOverLimit>);
}

ZEST_CASE(table_shaped_structs_roundtrip) {
    const Placed<WithOptional> optional{
        .entries = {{.cached = 7, .id = 1}, {.cached = std::nullopt, .id = 2}},
        .solo = {.cached = 3,            .id = 3                          },
    };
    const Placed<WithLongDouble> wide{
        .entries = {{.ratio = 2.5L, .id = 1}},
        .solo = {.ratio = 0.25L, .id = 3},
    };
    const Placed<WithDeducedEnum> deduced{
        .entries = {{.mode = mode_on, .id = 1}},
        .solo = {.mode = mode_on, .id = 3},
    };
    const Placed<CellProbe> probes{
        .entries = {{.kind = ProbeKind::light, .reading = 1.5F}},
        .solo = {.kind = ProbeKind::heat, .reading = -2.0F},
    };
    auto check = [](const auto& input) {
        using T = std::remove_cvref_t<decltype(input)>;
        ZEST_CONTEXT("{}", meta::type_name<T>());
        auto bytes = fbs::to_bytes(input);
        ZASSERT(bytes);
        ZEXPECT(solo_is_a_table(*bytes));
        auto decoded = fbs::from_bytes<T>(*bytes);
        ZASSERT(decoded);
        ZEXPECT(decoded->entries == input.entries);
        ZEXPECT(decoded->solo == input.solo);
    };
    check(optional);
    check(wide);
    check(deduced);
    check(probes);
}

ZEST_CASE(annotated_member_keeps_its_adapter_at_any_depth) {
    // ProbeFrame's reflected fields are int32 cells, so without the
    // annotation guard it would inline as a memcpy image and never run the
    // adapter.
    struct Holder {
        ProbeFrame frame;
    };

    Holder input{};
    input.frame.probe.cal.code = 27;
    input.frame.id = 4;
    auto bytes = fbs::to_bytes(input);
    ZASSERT(bytes);
    const auto* frame = root_of(*bytes)->GetPointer<const fbs::Table*>(slot(0));
    ZASSERT(frame != nullptr);
    const auto* probe = frame->GetPointer<const fbs::Table*>(slot(0));
    ZASSERT(probe != nullptr);
    const auto* cal = probe->GetPointer<const fbs::String*>(slot(0));
    ZASSERT(cal != nullptr);
    ZEXPECT(cal->str() == "27");
    auto decoded = fbs::from_bytes<Holder>(*bytes);
    ZASSERT(decoded);
    ZEXPECT(decoded->frame == input.frame);
}

ZEST_CASE(non_assignable_struct_decodes_field_by_field) {
    // Copying a Pinned is deprecated, so the struct holds one only as a
    // field and the roundtrip never needs the whole object.
    ZSTATIC_EXPECT(std::is_trivially_copyable_v<Pinned>);
    ZSTATIC_EXPECT(!fbs::can_inline_struct_v<Pinned>);

    struct Holder {
        Pinned solo;
        std::int32_t tail = 0;
    };

    const Holder input{
        .solo = {.tag = 's', .id = 7},
        .tail = 3
    };
    auto bytes = fbs::to_bytes(input);
    ZASSERT(bytes);
    ZEXPECT(root_of(*bytes)->GetPointer<const fbs::Table*>(slot(0)) != nullptr);
    Holder decoded{};
    ZASSERT(fbs::from_bytes(*bytes, decoded));
    ZEXPECT(decoded.solo == input.solo);
    ZEXPECT(decoded.tail == input.tail);
}

ZEST_CASE(inline_struct_padding_is_zero) {
    // The same values with dirty and clean padding, through every inline
    // write path: a field, a vector element and a map key.
    // A map key is const in storage and cannot be scribbled again; the check
    // on the buffer below covers its path whatever padding it kept.
    WithPadded noisy;
    noisy.solo = {.tag = 'x', .id = 7};
    noisy.items.push_back({.tag = 'y', .id = 9});
    noisy.scores.emplace(scribbled(0xFF, 'k', 3), 1);
    scribble(noisy.solo, 0xFF);
    scribble(noisy.items[0], 0xFF);

    WithPadded quiet;
    quiet.solo = {.tag = 'x', .id = 7};
    quiet.items.push_back({.tag = 'y', .id = 9});
    quiet.scores.emplace(Padded{.tag = 'k', .id = 3}, 1);

    auto noisy_bytes = fbs::to_bytes(noisy);
    ZASSERT(noisy_bytes);
    auto quiet_bytes = fbs::to_bytes(quiet);
    ZASSERT(quiet_bytes);
    // Anything short of identical bytes would disclose the memory under the
    // padding and make encoding nondeterministic.
    ZEXPECT(*noisy_bytes == *quiet_bytes);

    const auto* root = root_of(*noisy_bytes);
    const auto* items = root->GetPointer<const fbs::Vector<const Padded*>*>(slot(0));
    ZASSERT(items != nullptr);
    ZASSERT(items->size() == 1U);
    ZEXPECT(padding_is_zero(items->Get(0)));
    const auto* solo = root->GetStruct<const Padded*>(slot(1));
    ZASSERT(solo != nullptr);
    ZEXPECT(padding_is_zero(solo));
    const auto* entries = root->GetPointer<const EntryVector*>(slot(2));
    ZASSERT(entries != nullptr);
    ZASSERT(entries->size() == 1U);
    const auto* key = entries->Get(0)->GetStruct<const Padded*>(slot(0));
    ZASSERT(key != nullptr);
    ZEXPECT(padding_is_zero(key));

    auto decoded = fbs::from_bytes<WithPadded>(*noisy_bytes);
    ZASSERT(decoded);
    ZEXPECT(decoded->solo == quiet.solo);
    ZEXPECT(decoded->items == quiet.items);
}

ZEST_CASE(inline_struct_with_default_initializers_stays_inline) {
    ZSTATIC_EXPECT(fbs::can_inline_struct_v<SentinelRange>);
    ZSTATIC_EXPECT(fbs::can_inline_struct_v<OccurrenceKey>);
    const Placed<OccurrenceKey> input{
        .entries = {{.range = {.begin = 1, .end = 5}, .target = 9, .weight = 0}},
        .solo = {.range = {.begin = 8, .end = 9}, .target = 1, .weight = 2},
    };
    auto bytes = fbs::to_bytes(input);
    ZASSERT(bytes);
    const auto* solo = root_of(*bytes)->GetStruct<const OccurrenceKey*>(slot(1));
    ZASSERT(solo != nullptr);
    ZEXPECT(solo->range.end == 9U);
    auto decoded = fbs::from_bytes<Placed<OccurrenceKey>>(*bytes);
    ZASSERT(decoded);
    ZEXPECT(decoded->entries == input.entries);
    ZEXPECT(decoded->solo == input.solo);
}

ZEST_CASE(string_repr_elements_are_strings) {
    // An iterable class and an adapted int both travel as strings, so the
    // vector is one of strings, not of their raw shape.
    using Decimal = meta::annotation<int, meta::behavior::with<test::DecimalText>>;

    struct Lists {
        std::vector<IdSet> groups;
        std::vector<Decimal> numbers;
    };

    const Lists input{
        .groups = {IdSet{{10, 20}}, IdSet{}, IdSet{{7}}},
        .numbers = {12, -3},
    };
    auto bytes = fbs::to_bytes(input);
    ZASSERT(bytes);
    const auto* root = root_of(*bytes);
    const auto* groups = root->GetPointer<const fbs::Vector<fbs::offset_t<fbs::String>>*>(slot(0));
    ZASSERT(groups != nullptr);
    ZASSERT(groups->size() == 3U);
    ZEXPECT(groups->Get(0)->str() == "10,20");
    const auto* numbers = root->GetPointer<const fbs::Vector<fbs::offset_t<fbs::String>>*>(slot(1));
    ZASSERT(numbers != nullptr);
    ZASSERT(numbers->size() == 2U);
    ZEXPECT(numbers->Get(1)->str() == "-3");
    auto decoded = fbs::from_bytes<Lists>(*bytes);
    ZASSERT(decoded);
    ZEXPECT(decoded->groups == input.groups);
    ZEXPECT(meta::eq(decoded->numbers, input.numbers));
}

ZEST_CASE(table_repr_elements_are_tables) {
    const std::vector<Endpoint> input{
        {"alpha", 1  },
        {"",      0  },
        {"beta",  443}
    };
    auto bytes = fbs::to_bytes(input);
    ZASSERT(bytes);
    const auto* tables = root_of(*bytes)->GetPointer<const EntryVector*>(slot(0));
    ZASSERT(tables != nullptr);
    ZASSERT(tables->size() == 3U);
    ZEXPECT(tables->Get(2)->GetField<std::uint32_t>(slot(1), 0) == 443U);
    auto decoded = fbs::from_bytes<std::vector<Endpoint>>(*bytes);
    ZASSERT(decoded);
    ZEXPECT(*decoded == input);
}

ZEST_CASE(nullable_null_and_bytes_repr_elements_are_boxed) {
    // A nullable, a null and a byte-blob repr each need a table per element,
    // like the plain optionals and byte vectors they travel as; a null
    // element still takes its entry, so the count survives.
    struct Lists {
        std::vector<test::Lamport> stamps;
        std::vector<Marker> markers;
        std::vector<ByteBag> blobs;
        std::map<std::uint32_t, ByteBag> blobs_by_id;
    };

    const Lists input{
        .stamps = {{.tick = 7}, {.tick = 0}, {.tick = 42}},
        .markers = {{}, {}, {}},
        .blobs = {ByteBag{{std::byte{0xAA}, std::byte{0xBB}}}, ByteBag{}},
        .blobs_by_id = {{10, ByteBag{{std::byte{0x11}}}}},
    };
    auto bytes = fbs::to_bytes(input);
    ZASSERT(bytes);
    const auto* root = root_of(*bytes);
    const auto* stamps = root->GetPointer<const EntryVector*>(slot(0));
    ZASSERT(stamps != nullptr);
    ZASSERT(stamps->size() == 3U);
    ZEXPECT(stamps->Get(0)->GetField<std::uint32_t>(slot(0), 0) == 7U);
    ZEXPECT(stamps->Get(1)->GetOptionalFieldOffset(slot(0)) == 0U);
    const auto* markers = root->GetPointer<const EntryVector*>(slot(1));
    ZASSERT(markers != nullptr);
    ZEXPECT(markers->size() == 3U);
    const auto* blobs = root->GetPointer<const EntryVector*>(slot(2));
    ZASSERT(blobs != nullptr);
    ZASSERT(blobs->size() == 2U);
    const auto* first_blob = blobs->Get(0)->GetPointer<const fbs::Vector<std::uint8_t>*>(slot(0));
    ZASSERT(first_blob != nullptr);
    ZEXPECT(first_blob->size() == 2U);

    auto decoded = fbs::from_bytes<Lists>(*bytes);
    ZASSERT(decoded);
    ZEXPECT(decoded->stamps == input.stamps);
    ZEXPECT(decoded->markers.size() == 3U);
    ZEXPECT(decoded->blobs == input.blobs);
    ZEXPECT(decoded->blobs_by_id == input.blobs_by_id);
}

ZEST_CASE(format_scoped_repr_elements_are_scalars) {
    // Journal's fbs repr is an integer: its elements are integer cells.
    const std::vector<test::Journal> input{{.page = 3}, {.page = 9}};
    auto bytes = fbs::to_bytes(input);
    ZASSERT(bytes);
    const auto* cells = root_of(*bytes)->GetPointer<const fbs::Vector<std::int64_t>*>(slot(0));
    ZASSERT(cells != nullptr);
    ZASSERT(cells->size() == 2U);
    ZEXPECT(cells->Get(1) == 9);
}

ZEST_CASE(nested_table_encode_error_fails) {
    // An error inside a nested table fails the encode instead of leaving a
    // null offset for the builder to trip over.
    const LabelledReading bad{.value = std::numeric_limits<double>::quiet_NaN(), .unit = "c"};

    ReadingLog element;
    element.readings = {{}, bad};
    auto in_element = fbs::to_bytes<test::NanErrorConfig>(element);
    ZASSERT(!in_element);
    ZEXPECT(in_element.error().message == "NaN or Infinity is not allowed");
    ZEXPECT(in_element.error().format_path() == "readings[1].value");

    ReadingLog map_value;
    map_value.by_name = {
        {"a", bad}
    };
    auto in_map_value = fbs::to_bytes<test::NanErrorConfig>(map_value);
    ZASSERT(!in_map_value);
    ZEXPECT(in_map_value.error().message == "NaN or Infinity is not allowed");

    ReadingLog tuple_field;
    tuple_field.numbered = {1, bad};
    auto in_tuple_field = fbs::to_bytes<test::NanErrorConfig>(tuple_field);
    ZASSERT(!in_tuple_field);
    ZEXPECT(in_tuple_field.error().message == "NaN or Infinity is not allowed");
    ZEXPECT(in_tuple_field.error().format_path() == "numbered[1].value");

    ReadingLog tuple_element;
    tuple_element.numbered_list = {
        NumberedReading{1, {} },
        NumberedReading{2, bad}
    };
    auto in_tuple_element = fbs::to_bytes<test::NanErrorConfig>(tuple_element);
    ZASSERT(!in_tuple_element);
    ZEXPECT(in_tuple_element.error().message == "NaN or Infinity is not allowed");
    ZEXPECT(in_tuple_element.error().format_path() == "numbered_list[1][1].value");

    auto in_root_tuple =
        fbs::to_bytes<test::NanErrorConfig>(std::tuple<int, LabelledReading>{1, bad});
    ZASSERT(!in_root_tuple);
    ZEXPECT(in_root_tuple.error().format_path() == "[1].value");
}

ZEST_CASE(nan_repr_does_not_reach_inline_structs) {
    // Known gap: an inline struct travels as its image, so the protocol never
    // sees its floats, and nan_repr::Error and nan_repr::Null do not apply to
    // them. The layout depends on the type alone, so the struct cannot turn
    // into a table under a config; whether such configs should be rejected
    // for types holding inline floats, or checked against the image, is open.
    // The case pins today's behaviour.
    ZSTATIC_EXPECT(fbs::can_inline_struct_v<Reading>);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    auto inline_error = fbs::to_bytes<test::NanErrorConfig>(test::Field<Reading>{
        {.value = nan, .error = 0}
    });
    ZEXPECT(inline_error.has_value());
    auto loose_error = fbs::to_bytes<test::NanErrorConfig>(test::Field<double>{nan});
    ZASSERT(!loose_error);
    ZEXPECT(loose_error.error().message == "NaN or Infinity is not allowed");
}

};  // ZEST_SUITE(codec_fbs_encode)

}  // namespace

}  // namespace kota::codec
