#if __has_include(<flatbuffers/flatbuffers.h>)

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/meta/attrs.h"
#include "kota/codec/fbs/fbs.h"

// Corrupt-input coverage for the fbs verifier: the eager path
// (fbs::from_bytes) verifies every raw access while it reads, the zero-copy
// path (table_view::from_bytes) deep-verifies the buffer upfront. Either way
// the contract is the same: hostile bytes produce an error or an invalid
// view, never an out-of-bounds read — the ASan tree is the authority on the
// "never" part, these tests drive the inputs.

namespace kota_fbs_verifier_test {

// Decoded through imperative reprs whose deserialize bodies deliberately
// ignore has_element()/has_entry() and read a fixed count of four — the
// readers themselves must reject the cursor once it passes the verified
// size instead of reading out of bounds.
struct greedy_seq {
    std::vector<std::int32_t> nums;

    auto operator==(const greedy_seq&) const -> bool = default;
};

struct greedy_map {
    std::map<std::string, std::int32_t> entries;

    auto operator==(const greedy_map&) const -> bool = default;
};

}  // namespace kota_fbs_verifier_test

namespace kota::meta {

template <>
struct repr<kota_fbs_verifier_test::greedy_seq> {
    using type = std::vector<std::int32_t>;

    const static type& to(const kota_fbs_verifier_test::greedy_seq& g) {
        return g.nums;
    }

    template <typename Config>
    static bool deserialize(auto& vis, kota_fbs_verifier_test::greedy_seq& g) {
        g.nums.clear();
        return vis.visit_seq(g.nums, [&](auto& sv) -> bool {
            for(std::size_t i = 0; i < 4; ++i) {
                std::int32_t v = 0;
                if(!sv.visit_element([&](auto& ev) -> bool { return ev.visit_int(v); }))
                    return false;
                g.nums.push_back(v);
            }
            return true;
        });
    }
};

template <>
struct repr<kota_fbs_verifier_test::greedy_map> {
    using type = std::map<std::string, std::int32_t>;

    const static type& to(const kota_fbs_verifier_test::greedy_map& g) {
        return g.entries;
    }

    template <typename Config>
    static bool deserialize(auto& vis, kota_fbs_verifier_test::greedy_map& g) {
        g.entries.clear();
        return vis.visit_map(g.entries, [&](auto& mv) -> bool {
            for(std::size_t i = 0; i < 4; ++i) {
                std::string key;
                std::int32_t val = 0;
                bool ok = mv.visit_entry([&](auto& kv) -> bool { return kv.visit_str(key); },
                                         [&](auto& vv) -> bool { return vv.visit_int(val); });
                if(!ok)
                    return false;
                g.entries.emplace(std::move(key), val);
            }
            return true;
        });
    }
};

}  // namespace kota::meta

namespace kota::codec {

using namespace meta;

namespace {

using fbs::table_view;

struct inner {
    std::int32_t a = 0;
    std::string name;

    auto operator==(const inner&) const -> bool = default;
};

struct point {
    std::int32_t x;
    std::int32_t y;

    auto operator==(const point&) const -> bool = default;
};

enum class grade : std::int32_t {
    low = 0,
    mid = 1,
    high = 2,
};

/// One field of every layout the verifier classifies: scalars, strings,
/// string/table/scalar/inline-struct vectors, a map, an optional, a variant,
/// bytes, and a tuple.
struct rich {
    std::int32_t id = 0;
    std::string title;
    std::vector<std::string> tags;
    std::vector<inner> items;
    std::vector<std::int32_t> nums;
    std::vector<point> pts;
    std::map<std::string, inner> index;
    std::optional<std::int32_t> opt;
    std::variant<std::int32_t, std::string, inner> which;
    std::vector<std::byte> blob;
    std::tuple<std::int32_t, std::string> pair_like;

    auto operator==(const rich&) const -> bool = default;
};

auto make_rich() -> rich {
    return rich{
        .id = 42,
        .title = "torture",
        .tags = {"alpha", "beta", "gamma"},
        .items = {{.a = 1, .name = "one"}, {.a = 2, .name = "two"}},
        .nums = {10, 20, 30, 40},
        .pts = {{.x = 1, .y = 2}, {.x = 3, .y = 4}},
        .index = {{"k1", {.a = 7, .name = "seven"}}, {"k2", {.a = 8, .name = "eight"}}},
        .opt = 99,
        .which = std::string("chosen"),
        .blob = {std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}},
        .pair_like = {5, "five"},
    };
}

/// The shapes rich leaves out: bare enum/char/byte cells, a long double
/// (double cell), an inline struct field, optional-of-table, an integer-keyed
/// map, nested vectors, a set, a monostate alternative, std::array behind a
/// tuple, and the two slot-rerouting behavior attrs (as, enum_string).
struct rich2 {
    grade level = grade::low;
    char tag = 'x';
    std::byte flag{0};
    long double ratio = 0.0L;
    point pos;
    std::optional<inner> extra;
    std::unordered_map<std::uint64_t, std::string> names;
    std::vector<std::vector<std::int32_t>> grid;
    std::set<std::int32_t> uniq;
    std::variant<std::monostate, inner> maybe;
    std::tuple<std::int32_t, std::array<std::int32_t, 3>> mixed;
    annotation<std::int32_t, behavior::as<std::int64_t>> widened{0};
    annotation<grade, behavior::enum_string<rename_policy::identity>> level_name{grade::low};

    auto operator==(const rich2&) const -> bool = default;
};

auto make_rich2() -> rich2 {
    return rich2{
        .level = grade::mid,
        .tag = 'k',
        .flag = std::byte{0x5A},
        .ratio = 2.5L,
        .pos = {.x = 3, .y = 4},
        .extra = inner{.a = 6, .name = "boxed"},
        .names = {{7u, "seven"}, {8u, "eight"}},
        .grid = {{1, 2}, {3}},
        .uniq = {5, 9},
        .maybe = inner{.a = 1, .name = "table payload"},
        .mixed = {11, {21, 22, 23}},
        .widened = {1234},
        .level_name = {grade::high},
    };
}

struct node {
    std::int32_t value = 0;
    std::unique_ptr<node> next;
};

/// weak_ptr is the third nullable smart pointer the encoder locks and writes
/// like shared_ptr; the classification (deep_clean_t) must peel it the same
/// way or the verifier checks the slot as a table offset instead of the
/// pointee's layout.
struct weak_holder {
    std::int32_t before = 0;
    std::weak_ptr<std::int32_t> num;
    std::string after;
};

struct occ_key {
    std::uint32_t begin = static_cast<std::uint32_t>(-1);
    std::uint32_t end = static_cast<std::uint32_t>(-1);
    std::uint64_t target = 0;

    friend bool operator==(const occ_key&, const occ_key&) = default;
};

struct occ_key_less {
    bool operator()(const occ_key& a, const occ_key& b) const {
        return std::tie(a.begin, a.end, a.target) < std::tie(b.begin, b.end, b.target);
    }
};

/// Struct-keyed map: the entry vector holds inline-struct key cells the
/// binary search reads back, so tampered bytes reach both the key and the
/// value slots.
struct struct_keyed {
    std::map<occ_key, std::int32_t, occ_key_less> hits;

    friend bool operator==(const struct_keyed&, const struct_keyed&) = default;
};

/// Bool-bearing inline struct: size/alignment verification admits any byte
/// image, so both decode paths must additionally reject a bool byte other
/// than 0 or 1 — copying such an image into a live bool would be undefined
/// behavior to read.
struct bool_flags {
    bool ready = false;
    std::int32_t code = 0;
};

/// Nests the bool one struct level down, putting the validator's offset
/// accumulation on the hook: the byte sits at inner's offset plus the
/// field's own.
struct nested_bool {
    std::int32_t id = 0;
    bool_flags inner;
};

struct with_bool_structs {
    bool_flags solo;
    std::vector<bool_flags> items;
    nested_bool deep;
};

static_assert(fbs::can_inline_struct_v<bool_flags>);
static_assert(fbs::can_inline_struct_v<nested_bool>);

auto make_chain(std::size_t depth) -> node {
    node head{.value = 0, .next = nullptr};
    node* tail = &head;
    for(std::size_t i = 1; i < depth; ++i) {
        tail->next = std::make_unique<node>();
        tail->next->value = static_cast<std::int32_t>(i);
        tail = tail->next.get();
    }
    return head;
}

/// Drives both hostile-input families over one fixture.
///
/// Truncation: chopping the tail removes data the root offset still points
/// into, so any prefix must either fail verification or — when only trailing
/// padding fell off — still decode to the original value.
///
/// Single-byte corruption: flipped values may still decode — a corrupted
/// scalar is in bounds — but must never crash. When the upfront walk accepts
/// a tampered buffer, `probe` exercises the view's offset-bearing accessors;
/// the ASan preset turns any escape into a hard failure.
template <typename T, typename Probe>
void expect_hostile_bytes_contained(const T& input, Probe&& probe) {
    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    for(std::size_t len = 8; len < encoded->size(); ++len) {
        auto span = std::span<const std::uint8_t>(encoded->data(), len);
        auto result = fbs::from_bytes<T>(span);
        if(result.has_value()) {
            EXPECT(*result == input);
        }
        auto root = table_view<T>::from_bytes(span);
        if(root.valid()) {
            probe(root);
        }
    }

    for(std::size_t pos = 0; pos < encoded->size(); ++pos) {
        auto tampered = *encoded;
        tampered[pos] ^= 0xFF;

        T sink{};
        [[maybe_unused]] auto result = fbs::from_bytes(tampered, sink);

        auto root = table_view<T>::from_bytes(tampered);
        if(root.valid()) {
            probe(root);
        }
    }
}

ZEST_SUITE(codec_fbs_flatbuffers_verifier) {

ZEST_CASE(rich_fixture_round_trips_both_paths) {
    const auto input = make_rich();
    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    auto decoded = fbs::from_bytes<rich>(*encoded);
    ASSERT(decoded);
    EXPECT(*decoded == input);

    auto root = table_view<rich>::from_bytes(*encoded);
    ASSERT(root.valid());
    EXPECT(root[&rich::id] == 42);
    EXPECT(root[&rich::title] == "torture");
    EXPECT(root[&rich::tags].size() == 3U);
    EXPECT(root[&rich::index]["k2"][&inner::name] == "eight");
    EXPECT(root[&rich::which].get<1>() == "chosen");
}

ZEST_CASE(rich2_fixture_round_trips_both_paths) {
    const auto input = make_rich2();
    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    auto decoded = fbs::from_bytes<rich2>(*encoded);
    ASSERT(decoded);
    EXPECT(*decoded == input);

    auto root = table_view<rich2>::from_bytes(*encoded);
    ASSERT(root.valid());
    EXPECT(root[&rich2::level] == grade::mid);
    EXPECT(root[&rich2::tag] == 'k');
    EXPECT(root[&rich2::flag] == std::byte{0x5A});
    EXPECT(root[&rich2::pos].y == 4);
    EXPECT(root[&rich2::extra][&inner::name] == "boxed");
    EXPECT(root[&rich2::names][std::uint64_t{8}] == "eight");
    EXPECT(root[&rich2::grid][0][1] == 2);
    EXPECT(root[&rich2::uniq].size() == 2U);
    EXPECT(root[&rich2::maybe].index() == 1U);
    EXPECT(root[&rich2::maybe].get<1>()[&inner::a] == 1);
    EXPECT(root[&rich2::mixed].get<1>().get<2>() == 23);
    // Behavior attrs reroute the slot: as<int64> reads back the widened cell,
    // enum_string reads back the enumerator's name.
    EXPECT(root[&rich2::widened] == 1234);
    EXPECT(root[&rich2::level_name] == "high");
}

ZEST_CASE(monostate_alternative_round_trips_both_paths) {
    // A selected monostate travels as an empty table at the payload slot;
    // the views read the slot as a zero-size inline struct. Both must stay
    // in bounds.
    rich2 input = make_rich2();
    input.maybe = std::monostate{};

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    auto decoded = fbs::from_bytes<rich2>(*encoded);
    ASSERT(decoded);
    EXPECT(*decoded == input);

    auto root = table_view<rich2>::from_bytes(*encoded);
    ASSERT(root.valid());
    EXPECT(root[&rich2::maybe].index() == 0U);
    [[maybe_unused]] auto unit = root[&rich2::maybe].get<0>();
}

ZEST_CASE(weak_ptr_field_verifies_and_reads) {
    auto owner = std::make_shared<std::int32_t>(77);
    weak_holder input{.before = 5, .num = owner, .after = "tail"};

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    auto root = table_view<weak_holder>::from_bytes(*encoded);
    ASSERT(root.valid());
    EXPECT(root[&weak_holder::before] == 5);
    EXPECT(root[&weak_holder::num] == 77);
    EXPECT(root[&weak_holder::after] == "tail");

    // An expired weak_ptr leaves its slot absent; the view reads the default.
    owner.reset();
    ASSERT(input.num.expired());
    auto absent = fbs::to_bytes(input);
    ASSERT(absent);

    auto root2 = table_view<weak_holder>::from_bytes(*absent);
    ASSERT(root2.valid());
    EXPECT(root2[&weak_holder::num] == 0);
}

ZEST_CASE(undersized_buffers_are_rejected) {
    // Anything below root-uoffset + identifier cannot be a flatbuffer.
    std::vector<std::uint8_t> tiny(8, 0xAB);
    for(std::size_t len = 0; len < tiny.size(); ++len) {
        auto span = std::span<const std::uint8_t>(tiny.data(), len);
        auto result = fbs::from_bytes<rich>(span);
        ASSERT(!result);
        EXPECT(!table_view<rich>::from_bytes(span).valid());
    }
}

ZEST_CASE(minimal_buffers_with_valid_identifier_are_rejected) {
    // Smallest spans that pass the size and identifier gates; the root
    // offset then points into the identifier or at the buffer end.
    const std::array<std::uint8_t, 8> into_identifier = {4, 0, 0, 0, 'E', 'V', 'T', 'O'};
    const std::array<std::uint8_t, 8> at_end = {8, 0, 0, 0, 'E', 'V', 'T', 'O'};

    for(const auto& raw: {into_identifier, at_end}) {
        auto span = std::span<const std::uint8_t>(raw.data(), raw.size());
        EXPECT(!fbs::from_bytes<rich>(span).has_value());
        EXPECT(!table_view<rich>::from_bytes(span).valid());
    }
}

ZEST_CASE(wrong_identifier_is_rejected) {
    auto encoded = fbs::to_bytes(make_rich());
    ASSERT(encoded);

    auto tampered = *encoded;
    tampered[4] ^= 0xFF;

    EXPECT(!fbs::from_bytes<rich>(tampered).has_value());
    EXPECT(!table_view<rich>::from_bytes(tampered).valid());
}

ZEST_CASE(out_of_bounds_root_offset_is_rejected) {
    auto encoded = fbs::to_bytes(make_rich());
    ASSERT(encoded);

    auto tampered = *encoded;
    tampered[0] = 0xFF;
    tampered[1] = 0xFF;
    tampered[2] = 0xFF;
    tampered[3] = 0x7F;

    EXPECT(!fbs::from_bytes<rich>(tampered).has_value());
    EXPECT(!table_view<rich>::from_bytes(tampered).valid());
}

ZEST_CASE(hostile_bytes_never_read_out_of_bounds_rich) {
    expect_hostile_bytes_contained(make_rich(), [](const table_view<rich>& root) {
        [[maybe_unused]] auto title = root[&rich::title];
        auto tags = root[&rich::tags];
        for(std::size_t i = 0; i < tags.size(); ++i) {
            [[maybe_unused]] auto tag = tags[i];
        }
        auto items = root[&rich::items];
        for(std::size_t i = 0; i < items.size(); ++i) {
            [[maybe_unused]] auto name = items[i][&inner::name];
        }
        [[maybe_unused]] auto hit = root[&rich::index]["k1"];
        [[maybe_unused]] auto chosen = root[&rich::which].get<1>();
        [[maybe_unused]] auto second = root[&rich::pair_like].get<1>();
    });
}

ZEST_CASE(hostile_bytes_never_read_out_of_bounds_rich2) {
    expect_hostile_bytes_contained(make_rich2(), [](const table_view<rich2>& root) {
        [[maybe_unused]] auto level = root[&rich2::level];
        [[maybe_unused]] auto pos = root[&rich2::pos];
        [[maybe_unused]] auto extra_name = root[&rich2::extra][&inner::name];
        [[maybe_unused]] auto lookup = root[&rich2::names][std::uint64_t{7}];
        auto grid = root[&rich2::grid];
        for(std::size_t i = 0; i < grid.size(); ++i) {
            auto row = grid[i];
            for(std::size_t j = 0; j < row.size(); ++j) {
                [[maybe_unused]] auto cell = row[j];
            }
        }
        [[maybe_unused]] auto uniq_size = root[&rich2::uniq].size();
        [[maybe_unused]] auto payload = root[&rich2::maybe].get<1>();
        [[maybe_unused]] auto arr = root[&rich2::mixed].get<1>().get<0>();
        [[maybe_unused]] auto widened = root[&rich2::widened];
        [[maybe_unused]] auto level_name = root[&rich2::level_name];
    });
}

ZEST_CASE(hostile_bytes_never_read_out_of_bounds_struct_keyed_map) {
    struct_keyed input;
    input.hits.emplace(occ_key{.begin = 1, .end = 5, .target = 9}, 1);
    input.hits.emplace(occ_key{.begin = 1, .end = 6, .target = 0}, 2);
    input.hits.emplace(occ_key{.begin = 2, .end = 0, .target = 3}, 3);

    expect_hostile_bytes_contained(input, [](const table_view<struct_keyed>& root) {
        auto m = root[&struct_keyed::hits];
        for(std::size_t i = 0; i < m.size(); ++i) {
            auto entry = m.at(i);
            [[maybe_unused]] auto key = entry.get<0>();
            [[maybe_unused]] auto value = entry.get<1>();
        }
        // The binary search walks tampered key cells.
        [[maybe_unused]] auto hit = m[occ_key{.begin = 1, .end = 5, .target = 9}];
        [[maybe_unused]] bool present = m.contains(occ_key{.begin = 9, .end = 9, .target = 9});
    });
}

ZEST_CASE(inline_struct_bool_byte_must_be_zero_or_one) {
    const with_bool_structs input{
        .solo = {.ready = true,               .code = 7                          },
        .items = {{.ready = false, .code = 1}, {.ready = true, .code = 2}         },
        .deep = {.id = 3,                     .inner = {.ready = true, .code = 4}},
    };

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);
    ASSERT(fbs::from_bytes<with_bool_structs>(*encoded).has_value());

    // Locate the stored images through the raw accessors; slots follow
    // declaration order (solo=4, items=6, deep=8).
    const auto* data = encoded->data();
    const auto* root = ::flatbuffers::GetRoot<::flatbuffers::Table>(data);
    const auto* solo = root->GetStruct<const bool_flags*>(4);
    const auto* items = root->GetPointer<const ::flatbuffers::Vector<const bool_flags*>*>(6);
    const auto* deep = root->GetStruct<const nested_bool*>(8);
    ASSERT((solo != nullptr && items != nullptr && deep != nullptr));

    const auto byte_at = [&](const void* stored, std::size_t offset) {
        return static_cast<std::size_t>(static_cast<const std::uint8_t*>(stored) - data) + offset;
    };

    // Any byte but 0/1 must fail both paths — at a field slot, inside the
    // struct vector, and behind the nested field's offset.
    for(std::size_t pos:
        {byte_at(solo, offsetof(bool_flags, ready)),
         byte_at(items->Get(1), offsetof(bool_flags, ready)),
         byte_at(deep, offsetof(nested_bool, inner) + offsetof(bool_flags, ready))}) {
        auto tampered = *encoded;
        tampered[pos] = 0x02;
        EXPECT(!fbs::from_bytes<with_bool_structs>(tampered).has_value());
        EXPECT(!table_view<with_bool_structs>::from_bytes(tampered).valid());
    }

    // The boundary value 1 stays a valid image and reads back as true.
    auto flipped = *encoded;
    flipped[byte_at(items->Get(0), offsetof(bool_flags, ready))] = 0x01;
    auto decoded = fbs::from_bytes<with_bool_structs>(flipped);
    ASSERT(decoded);
    EXPECT(decoded->items[0].ready);
    auto root_view = table_view<with_bool_structs>::from_bytes(flipped);
    ASSERT(root_view.valid());
    EXPECT(root_view[&with_bool_structs::items][0].ready);
}

ZEST_CASE(recursion_depth_boundary) {
    // The verifier's depth cap (flatbuffers default 64) is also what
    // terminates cyclic offsets: a cycle is just an infinitely deep chain.
    // Straddle the cap so a change to per-table depth cost surfaces here.
    auto shallow = fbs::to_bytes(make_chain(60));
    ASSERT(shallow);
    node shallow_out{};
    EXPECT(fbs::from_bytes(*shallow, shallow_out).has_value());
    EXPECT(table_view<node>::from_bytes(*shallow).valid());

    auto deep = fbs::to_bytes(make_chain(70));
    ASSERT(deep);
    node deep_out{};
    EXPECT(!fbs::from_bytes(*deep, deep_out).has_value());
    EXPECT(!table_view<node>::from_bytes(*deep).valid());
}

ZEST_CASE(imperative_adapter_cannot_overrun_vector) {
    using kota_fbs_verifier_test::greedy_seq;

    // The adapter reads four elements unconditionally; a valid two-element
    // buffer must fail the cursor check instead of reading past the vector.
    auto shorted = fbs::to_bytes(greedy_seq{
        .nums = {1, 2}
    });
    ASSERT(shorted);
    EXPECT(!fbs::from_bytes<greedy_seq>(*shorted).has_value());

    const greedy_seq exact{
        .nums = {1, 2, 3, 4}
    };
    auto encoded = fbs::to_bytes(exact);
    ASSERT(encoded);
    auto decoded = fbs::from_bytes<greedy_seq>(*encoded);
    ASSERT(decoded);
    EXPECT(*decoded == exact);
}

ZEST_CASE(imperative_adapter_cannot_overrun_map) {
    using kota_fbs_verifier_test::greedy_map;

    auto shorted = fbs::to_bytes(greedy_map{
        .entries = {{"a", 1}, {"b", 2}}
    });
    ASSERT(shorted);
    EXPECT(!fbs::from_bytes<greedy_map>(*shorted).has_value());

    const greedy_map exact{
        .entries = {{"a", 1}, {"b", 2}, {"c", 3}, {"d", 4}}
    };
    auto encoded = fbs::to_bytes(exact);
    ASSERT(encoded);
    auto decoded = fbs::from_bytes<greedy_map>(*encoded);
    ASSERT(decoded);
    EXPECT(*decoded == exact);
}

ZEST_CASE(byte_span_overload_rejects_hostile_input_too) {
    auto encoded = fbs::to_bytes(make_rich());
    ASSERT(encoded);

    auto tampered = *encoded;
    tampered[0] = 0xFF;
    tampered[1] = 0xFF;
    tampered[2] = 0xFF;
    tampered[3] = 0x7F;

    auto bytes = std::span<const std::byte>(reinterpret_cast<const std::byte*>(tampered.data()),
                                            tampered.size());
    rich sink{};
    EXPECT(!fbs::from_bytes(bytes, sink).has_value());
    EXPECT(!table_view<rich>::from_bytes(bytes).valid());
}

};  // ZEST_SUITE(codec_fbs_flatbuffers_verifier)

}  // namespace

}  // namespace kota::codec

#endif
