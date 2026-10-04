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

#include "codec/fbs/harness/struct_keys.h"
#include "codec/harness/fixtures/containers.h"
#include "codec/harness/fixtures/structs.h"
#include "kota/zest/zest.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"
#include "kota/meta/compare.h"
#include "kota/codec/fbs/fbs.h"

// What the fbs decoders do with bytes that did not come from to_bytes. The
// eager from_bytes verifies every access while it reads; table_view
// verifies the whole buffer up front. Either way corrupt, truncated or
// hostile input fails or yields an invalid view, never an out-of-bounds
// read: the sanitizer trees are the authority on "never", these cases drive
// the inputs. The kit's hostile_everything sweeps the eager decoder; the
// sweeps here also probe the views.

namespace kota::codec {

namespace {

/// Read through reprs whose imperative bodies ignore has_element() and
/// has_entry() and read four items whatever the buffer holds.
struct GreedySeq {
    std::vector<std::int32_t> nums;

    auto operator==(const GreedySeq&) const -> bool = default;
};

struct GreedyMap {
    std::map<std::string, std::int32_t> entries;

    auto operator==(const GreedyMap&) const -> bool = default;
};

/// Read back through visit_tuple over itself, though it is not tuple-like.
struct LabeledCount {
    std::int32_t count = 0;
    std::string label;

    auto operator==(const LabeledCount&) const -> bool = default;
};

struct HoldsLabeledCount {
    LabeledCount entry;
    std::int32_t after = 0;

    auto operator==(const HoldsLabeledCount&) const -> bool = default;
};

}  // namespace

}  // namespace kota::codec

namespace kota::meta {

template <>
struct repr<codec::GreedySeq> {
    using type = std::vector<std::int32_t>;

    const static type& to(const codec::GreedySeq& g) {
        return g.nums;
    }

    template <typename Config>
    static bool deserialize(auto& vis, codec::GreedySeq& g) {
        g.nums.clear();
        return vis.visit_seq(g.nums, [&](auto& sv) -> bool {
            for(std::size_t i = 0; i < 4; ++i) {
                std::int32_t v = 0;
                if(!sv.visit_element([&](auto& ev) -> bool { return ev.visit_int(v); })) {
                    return false;
                }
                g.nums.push_back(v);
            }
            return true;
        });
    }
};

template <>
struct repr<codec::GreedyMap> {
    using type = std::map<std::string, std::int32_t>;

    const static type& to(const codec::GreedyMap& g) {
        return g.entries;
    }

    template <typename Config>
    static bool deserialize(auto& vis, codec::GreedyMap& g) {
        g.entries.clear();
        return vis.visit_map(g.entries, [&](auto& mv) -> bool {
            for(std::size_t i = 0; i < 4; ++i) {
                std::string key;
                std::int32_t value = 0;
                if(!mv.visit_entry([&](auto& kv) -> bool { return kv.visit_str(key); },
                                   [&](auto& vv) -> bool { return vv.visit_int(value); })) {
                    return false;
                }
                g.entries.emplace(std::move(key), value);
            }
            return true;
        });
    }
};

template <>
struct repr<codec::LabeledCount> {
    using type = std::tuple<std::int32_t, std::string>;

    static type to(const codec::LabeledCount& c) {
        return {c.count, c.label};
    }

    template <typename Config>
    static bool deserialize(auto& vis, codec::LabeledCount& c) {
        return vis.visit_tuple(c, [&](auto& sv) -> bool {
            return sv.visit_element([&](auto& ev) -> bool { return ev.visit_int(c.count); }) &&
                   sv.visit_element([&](auto& ev) -> bool { return ev.visit_str(c.label); });
        });
    }
};

}  // namespace kota::meta

namespace kota::codec {

namespace {

using fbs::table_view;
using test::OccurrenceKey;
using test::Point;
using test::StructKeyed;

struct Inner {
    std::int32_t a = 0;
    std::string name;

    auto operator==(const Inner&) const -> bool = default;
};

enum class Grade : std::int32_t { low, mid, high };

/// One field of every layout the verifier classifies: scalar, enum, char,
/// byte and long double cells, strings, string, table, scalar and
/// inline-struct vectors, nested vectors, a set, string- and integer-keyed
/// maps, an inline struct, optionals of a scalar and of a table, variants
/// with a monostate and with a table, bytes, tuples with an std::array, and
/// the two slot-rerouting behavior attrs.
struct Rich {
    std::int32_t id = 0;
    std::string title;
    std::vector<std::string> tags;
    std::vector<Inner> items;
    std::vector<std::int32_t> nums;
    std::vector<Point> pts;
    std::map<std::string, Inner> index;
    std::optional<std::int32_t> opt;
    std::variant<std::int32_t, std::string, Inner> which;
    std::vector<std::byte> blob;
    std::tuple<std::int32_t, std::string> pair_like;
    Grade level = Grade::low;
    char tag = 'x';
    std::byte flag{0};
    long double ratio = 0.0L;
    Point pos;
    std::optional<Inner> extra;
    std::unordered_map<std::uint64_t, std::string> names;
    std::vector<std::vector<std::int32_t>> grid;
    std::set<std::int32_t> uniq;
    std::variant<std::monostate, Inner> maybe;
    std::tuple<std::int32_t, std::array<std::int32_t, 3>> mixed;
    meta::annotation<std::int32_t, meta::behavior::as<std::int64_t>> widened{0};
    meta::annotation<Grade, meta::behavior::enum_string<naming::rename_policy::identity>>
        level_name{Grade::low};

    auto operator==(const Rich&) const -> bool = default;
};

auto make_rich() -> Rich {
    return {
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
        .level = Grade::mid,
        .tag = 'k',
        .flag = std::byte{0x5A},
        .ratio = 2.5L,
        .pos = {.x = 3, .y = 4},
        .extra = Inner{.a = 6, .name = "boxed"},
        .names = {{7U, "seven"}, {8U, "eight"}},
        .grid = {{1, 2}, {3}},
        .uniq = {5, 9},
        .maybe = Inner{.a = 1, .name = "table payload"},
        .mixed = {11, {21, 22, 23}},
        .widened = {1234},
        .level_name = {Grade::high},
    };
}

/// Bool-bearing inline structs, one nesting the bool a level down so the
/// validator's offsets add up.
struct BoolFlags {
    bool ready = false;
    std::int32_t code = 0;
};

struct NestedBool {
    std::int32_t id = 0;
    BoolFlags inner;
};

struct WithBoolStructs {
    BoolFlags solo;
    std::vector<BoolFlags> items;
    NestedBool deep;
};

struct Node {
    std::int32_t value = 0;
    std::unique_ptr<Node> next;
};

struct MaybeAddress {
    std::variant<std::monostate, test::Address> maybe;
};

auto make_chain(std::size_t depth) -> Node {
    Node head{.value = 0, .next = nullptr};
    Node* tail = &head;
    for(std::size_t i = 1; i < depth; ++i) {
        tail->next = std::make_unique<Node>();
        tail->next->value = static_cast<std::int32_t>(i);
        tail = tail->next.get();
    }
    return head;
}

/// Every prefix either fails or, when only trailing padding fell off, still
/// decodes to the input; every byte flipped to its complement must not
/// crash either decoder. When a view accepts the bytes, probe() walks its
/// offset-bearing accessors.
template <typename T, typename Probe>
void expect_hostile_bytes_contained(const T& input, Probe probe) {
    auto encoded = fbs::to_bytes(input);
    ZASSERT(encoded);
    auto decoded = fbs::from_bytes<T>(*encoded);
    ZASSERT(decoded);
    ZEXPECT(*decoded == input);
    auto intact = table_view<T>::from_bytes(*encoded);
    ZASSERT(intact.valid());
    probe(intact);

    for(std::size_t size = 8; size < encoded->size(); ++size) {
        ZEST_CONTEXT("the first {} bytes", size);
        auto prefix = std::span<const std::uint8_t>(encoded->data(), size);
        auto result = fbs::from_bytes<T>(prefix);
        if(result) {
            ZEXPECT(*result == input);
        }
        if(auto root = table_view<T>::from_bytes(prefix); root.valid()) {
            probe(root);
        }
    }
    for(std::size_t at = 0; at < encoded->size(); ++at) {
        ZEST_CONTEXT("byte {} flipped", at);
        auto tampered = *encoded;
        tampered[at] ^= 0xFF;
        T sink{};
        [[maybe_unused]] auto result = fbs::from_bytes(tampered, sink);
        if(auto root = table_view<T>::from_bytes(tampered); root.valid()) {
            probe(root);
        }
    }
}

/// Whether the value-returning overload takes T.
template <typename T>
concept decodes_by_value =
    requires(std::span<const std::byte> bytes) { fbs::from_bytes<T>(bytes); };

ZEST_SUITE(codec_fbs_decode) {

ZEST_CASE(value_overload_value_initializes) {
    // `T value{}` would copy-list-initialize the explicit list from `{}`.
    const test::HoldsExplicit value{
        .list = {1, 2},
        .count = 2
    };
    auto encoded = fbs::to_bytes(value);
    ZASSERT(encoded);
    auto result = fbs::from_bytes<test::HoldsExplicit>(std::as_bytes(std::span(*encoded)));
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

ZEST_CASE(buffer_below_eight_bytes_fails) {
    // Anything shorter than a root offset and an identifier.
    std::vector<std::uint8_t> tiny(8, 0xAB);
    for(std::size_t size = 0; size < tiny.size(); ++size) {
        ZEST_CONTEXT("{} bytes", size);
        auto span = std::span<const std::uint8_t>(tiny.data(), size);
        auto result = fbs::from_bytes<Rich>(span);
        ZASSERT(!result);
        ZEXPECT(result.error().message == "buffer too small");
        ZEXPECT(!table_view<Rich>::from_bytes(span).valid());
    }
}

ZEST_CASE(wrong_identifier_fails) {
    auto encoded = fbs::to_bytes(make_rich());
    ZASSERT(encoded);
    auto tampered = *encoded;
    tampered[4] ^= 0xFF;
    auto result = fbs::from_bytes<Rich>(tampered);
    ZASSERT(!result);
    ZEXPECT(result.error().message == "invalid buffer identifier");
    ZEXPECT(!table_view<Rich>::from_bytes(tampered).valid());
}

ZEST_CASE(root_offset_outside_the_buffer_fails) {
    auto encoded = fbs::to_bytes(make_rich());
    ZASSERT(encoded);
    auto tampered = *encoded;
    tampered[0] = 0xFF;
    tampered[1] = 0xFF;
    tampered[2] = 0xFF;
    tampered[3] = 0x7F;
    auto result = fbs::from_bytes<Rich>(tampered);
    ZASSERT(!result);
    ZEXPECT(result.error().message == "buffer verification failed: root offset");
    ZEXPECT(!table_view<Rich>::from_bytes(tampered).valid());

    // The std::byte overloads verify the same way.
    auto bytes = std::as_bytes(std::span(tampered));
    Rich sink{};
    ZEXPECT(!fbs::from_bytes(bytes, sink));
    ZEXPECT(!table_view<Rich>::from_bytes(bytes).valid());
}

ZEST_CASE(root_table_in_the_identifier_fails) {
    // The smallest buffers past the size and identifier checks: the root
    // offset points into the identifier, or at the end.
    const std::array<std::uint8_t, 8> into_identifier = {4, 0, 0, 0, 'E', 'V', 'T', 'O'};
    auto into = fbs::from_bytes<Rich>(into_identifier);
    ZASSERT(!into);
    ZEXPECT(into.error().message == "buffer verification failed: root table");
    ZEXPECT(!table_view<Rich>::from_bytes(into_identifier).valid());

    const std::array<std::uint8_t, 8> at_end = {8, 0, 0, 0, 'E', 'V', 'T', 'O'};
    auto end = fbs::from_bytes<Rich>(at_end);
    ZASSERT(!end);
    ZEXPECT(end.error().message == "buffer verification failed: root offset");
    ZEXPECT(!table_view<Rich>::from_bytes(at_end).valid());
}

ZEST_CASE(hostile_bytes_stay_in_bounds) {
    expect_hostile_bytes_contained(make_rich(), [](const table_view<Rich>& root) {
        [[maybe_unused]] auto title = root[&Rich::title];
        auto tags = root[&Rich::tags];
        for(std::size_t i = 0; i < tags.size(); ++i) {
            [[maybe_unused]] auto tag = tags[i];
        }
        auto items = root[&Rich::items];
        for(std::size_t i = 0; i < items.size(); ++i) {
            [[maybe_unused]] auto name = items[i][&Inner::name];
        }
        [[maybe_unused]] auto hit = root[&Rich::index]["k1"];
        [[maybe_unused]] auto chosen = root[&Rich::which].get<1>();
        [[maybe_unused]] auto second = root[&Rich::pair_like].get<1>();
        [[maybe_unused]] auto level = root[&Rich::level];
        [[maybe_unused]] auto pos = root[&Rich::pos];
        [[maybe_unused]] auto extra_name = root[&Rich::extra][&Inner::name];
        [[maybe_unused]] auto lookup = root[&Rich::names][std::uint64_t{7}];
        auto grid = root[&Rich::grid];
        for(std::size_t i = 0; i < grid.size(); ++i) {
            auto row = grid[i];
            for(std::size_t j = 0; j < row.size(); ++j) {
                [[maybe_unused]] auto cell = row[j];
            }
        }
        [[maybe_unused]] auto uniq_size = root[&Rich::uniq].size();
        [[maybe_unused]] auto payload = root[&Rich::maybe].get<1>();
        [[maybe_unused]] auto element = root[&Rich::mixed].get<1>().get<0>();
        [[maybe_unused]] auto widened = root[&Rich::widened];
        [[maybe_unused]] auto level_name = root[&Rich::level_name];
    });
}

ZEST_CASE(hostile_bytes_stay_in_bounds_for_struct_keys) {
    StructKeyed input;
    input.hits.emplace(
        OccurrenceKey{
            .range = {.begin = 1, .end = 5},
            .target = 9,
            .weight = -2
    },
        1);
    input.hits.emplace(
        OccurrenceKey{
            .range = {.begin = 1, .end = 6},
            .target = 0,
            .weight = 3
    },
        2);
    input.hits.emplace(
        OccurrenceKey{
            .range = {.begin = 2, .end = 0},
            .target = 3,
            .weight = 0
    },
        3);
    expect_hostile_bytes_contained(input, [](const table_view<StructKeyed>& root) {
        auto hits = root[&StructKeyed::hits];
        for(std::size_t i = 0; i < hits.size(); ++i) {
            auto entry = hits.at(i);
            [[maybe_unused]] auto key = entry.get<0>();
            [[maybe_unused]] auto value = entry.get<1>();
        }
        // The binary search walks tampered key cells.
        [[maybe_unused]] auto hit = hits[OccurrenceKey{
            .range = {.begin = 1, .end = 5},
            .target = 9,
            .weight = -2
        }];
        [[maybe_unused]] bool present = hits.contains(OccurrenceKey{
            .range = {.begin = 9, .end = 9},
            .target = 9,
            .weight = 9
        });
    });
}

ZEST_CASE(inline_struct_bool_byte_other_than_zero_or_one_fails) {
    // Verification by size and alignment admits any image, but reading a
    // bool byte other than 0 or 1 is undefined behaviour.
    ZSTATIC_EXPECT(fbs::can_inline_struct_v<BoolFlags>);
    ZSTATIC_EXPECT(fbs::can_inline_struct_v<NestedBool>);
    const WithBoolStructs input{
        .solo = {.ready = true,               .code = 7                          },
        .items = {{.ready = false, .code = 1}, {.ready = true, .code = 2}         },
        .deep = {.id = 3,                     .inner = {.ready = true, .code = 4}},
    };
    auto encoded = fbs::to_bytes(input);
    ZASSERT(encoded);

    // Slots follow declaration order: solo at 4, items at 6, deep at 8.
    const auto* data = encoded->data();
    const auto* root = ::flatbuffers::GetRoot<fbs::Table>(data);
    const auto* solo = root->GetStruct<const BoolFlags*>(4);
    ZASSERT(solo != nullptr);
    const auto* items = root->GetPointer<const fbs::Vector<const BoolFlags*>*>(6);
    ZASSERT(items != nullptr);
    ZASSERT(items->size() == 2U);
    const auto* deep = root->GetStruct<const NestedBool*>(8);
    ZASSERT(deep != nullptr);
    auto byte_at = [&](const void* stored, std::size_t offset) {
        return static_cast<std::size_t>(static_cast<const std::uint8_t*>(stored) - data) + offset;
    };

    // In a field, in a struct vector, and behind a nested field's offset.
    for(std::size_t at: {byte_at(solo, offsetof(BoolFlags, ready)),
                         byte_at(items->Get(1), offsetof(BoolFlags, ready)),
                         byte_at(deep, offsetof(NestedBool, inner) + offsetof(BoolFlags, ready))}) {
        ZEST_CONTEXT("byte {}", at);
        auto tampered = *encoded;
        tampered[at] = 0x02;
        auto result = fbs::from_bytes<WithBoolStructs>(tampered);
        ZASSERT(!result);
        ZEXPECT(
            zest::starts_with(result.error().message, "buffer verification failed: inline struct"));
        ZEXPECT(!table_view<WithBoolStructs>::from_bytes(tampered).valid());
    }

    // 1 is a valid image and reads as true.
    auto flipped = *encoded;
    flipped[byte_at(items->Get(0), offsetof(BoolFlags, ready))] = 0x01;
    auto decoded = fbs::from_bytes<WithBoolStructs>(flipped);
    ZASSERT(decoded);
    ZEXPECT(decoded->items[0].ready);
    auto view = table_view<WithBoolStructs>::from_bytes(flipped);
    ZASSERT(view.valid());
    ZEXPECT(view[&WithBoolStructs::items][0].ready);
}

ZEST_CASE(monostate_payload_written_as_an_empty_table_reads) {
    // The encoder once wrote a variant's monostate payload as an empty table
    // at the alternative's slot; it now leaves the slot absent. A buffer
    // written the old way still reads, eagerly and through the views.
    ::flatbuffers::FlatBufferBuilder builder;
    auto empty = builder.EndTable(builder.StartTable());
    auto choice_start = builder.StartTable();
    builder.AddElement<std::uint32_t>(4, 0);
    builder.AddOffset(6, ::flatbuffers::Offset<void>(empty));
    auto choice = builder.EndTable(choice_start);
    auto root_start = builder.StartTable();
    builder.AddOffset(4, ::flatbuffers::Offset<void>(choice));
    builder.Finish(::flatbuffers::Offset<fbs::Table>(builder.EndTable(root_start)), "EVTO");
    const std::span<const std::uint8_t> bytes(builder.GetBufferPointer(), builder.GetSize());

    // Decoded over the other alternative, which the monostate replaces.
    MaybeAddress decoded{
        .maybe = test::Address{.city = "x", .zip = 1}
    };
    ZASSERT(fbs::from_bytes(bytes, decoded));
    ZEXPECT(decoded.maybe.index() == 0U);
    auto root = table_view<MaybeAddress>::from_bytes(bytes);
    ZASSERT(root.valid());
    ZEXPECT(root[&MaybeAddress::maybe].index() == 0U);
}

ZEST_CASE(nesting_deeper_than_64_tables_fails) {
    // The verifier's depth cap, flatbuffers' default of 64, also ends a
    // cycle of offsets, which is a chain without end. Straddle it so a
    // change in what a table costs shows here.
    auto shallow = fbs::to_bytes(make_chain(60));
    ZASSERT(shallow);
    Node shallow_out{};
    ZEXPECT(fbs::from_bytes(*shallow, shallow_out));
    ZEXPECT(table_view<Node>::from_bytes(*shallow).valid());

    auto deep = fbs::to_bytes(make_chain(70));
    ZASSERT(deep);
    Node deep_out{};
    auto result = fbs::from_bytes(*deep, deep_out);
    ZASSERT(!result);
    ZEXPECT(result.error().message == "buffer verification failed: struct field");
    ZEXPECT(!table_view<Node>::from_bytes(*deep).valid());
}

ZEST_CASE(imperative_adapter_cannot_read_past_a_vector) {
    // The adapter asks for four elements whatever the vector holds; the
    // reader refuses the third.
    auto shorted = fbs::to_bytes(GreedySeq{
        .nums = {1, 2}
    });
    ZASSERT(shorted);
    auto failed = fbs::from_bytes<GreedySeq>(*shorted);
    ZASSERT(!failed);
    ZEXPECT(failed.error().message == "buffer verification failed: vector element out of range");

    const GreedySeq exact{
        .nums = {1, 2, 3, 4}
    };
    auto encoded = fbs::to_bytes(exact);
    ZASSERT(encoded);
    auto decoded = fbs::from_bytes<GreedySeq>(*encoded);
    ZASSERT(decoded);
    ZEXPECT(*decoded == exact);
}

ZEST_CASE(imperative_adapter_cannot_read_past_a_map) {
    auto shorted = fbs::to_bytes(GreedyMap{
        .entries = {{"a", 1}, {"b", 2}}
    });
    ZASSERT(shorted);
    auto failed = fbs::from_bytes<GreedyMap>(*shorted);
    ZASSERT(!failed);
    ZEXPECT(failed.error().message == "buffer verification failed: map entry out of range");

    const GreedyMap exact{
        .entries = {{"a", 1}, {"b", 2}, {"c", 3}, {"d", 4}}
    };
    auto encoded = fbs::to_bytes(exact);
    ZASSERT(encoded);
    auto decoded = fbs::from_bytes<GreedyMap>(*encoded);
    ZASSERT(decoded);
    ZEXPECT(*decoded == exact);
}

ZEST_CASE(imperative_adapter_reads_a_tuple_table_over_its_own_type) {
    // visit_tuple over a type that is not tuple-like: its slots are the
    // adapter's business, at the root and in a field.
    const LabeledCount root{.count = 3, .label = "three"};
    auto root_bytes = fbs::to_bytes(root);
    ZASSERT(root_bytes);
    auto root_back = fbs::from_bytes<LabeledCount>(*root_bytes);
    ZASSERT(root_back);
    ZEXPECT(*root_back == root);

    const HoldsLabeledCount holder{
        .entry = {.count = 5, .label = "five"},
        .after = 9
    };
    auto holder_bytes = fbs::to_bytes(holder);
    ZASSERT(holder_bytes);
    auto holder_back = fbs::from_bytes<HoldsLabeledCount>(*holder_bytes);
    ZASSERT(holder_back);
    ZEXPECT(*holder_back == holder);
}

};  // ZEST_SUITE(codec_fbs_decode)

}  // namespace

}  // namespace kota::codec
