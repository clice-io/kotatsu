#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kota/support/expected_try.h"
#include "kota/support/ranges.h"
#include "kota/meta/repr.h"
#include "kota/meta/type_kind.h"
#include "kota/codec/fbs/proxy.h"
#include "kota/codec/fbs/type.h"
#include "kota/codec/visit/config.h"
#include "kota/codec/visit/context.h"
#include "kota/codec/visit/encode.h"

namespace kota::codec::fbs {

namespace encode_detail {

using proxy_detail::slot_id;

/// The bytes an inline struct's fields occupy, recursing into nested inline
/// structs; falling short of sizeof means the native image carries padding.
/// Counting sizeof per leaf is exact because every admitted leaf scalar is
/// internally padding-free — long double, the one scalar whose image can
/// hold unspecified bytes, is excluded from inline structs entirely
/// (is_scalar_field_v).
template <typename T>
consteval auto packed_size() -> std::size_t {
    if constexpr(meta::reflectable_class<T>) {
        return []<std::size_t... I>(std::index_sequence<I...>) {
            return (std::size_t{0} + ... + packed_size<std::remove_cv_t<meta::field_type<T, I>>>());
        }(std::make_index_sequence<meta::field_count<T>()>{});
    } else {
        return sizeof(T);
    }
}

template <typename T>
constexpr bool has_padding_v = packed_size<T>() != sizeof(T);

template <typename T>
void copy_field_bytes(const T& value, std::byte* image) {
    if constexpr(meta::reflectable_class<T>) {
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (copy_field_bytes(meta::field_of<I>(value), image + meta::field_offset<T>(I)), ...);
        }(std::make_index_sequence<meta::field_count<T>()>{});
    } else {
        std::memcpy(image, std::addressof(value), sizeof(T));
    }
}

/// An inline struct's buffer image: T's size and alignment, byte-typed
/// contents. The bytes stay byte-typed all the way to the builder because a
/// copy of T itself may not survive intact — T's trivial copy operations
/// copy members, not the object representation, so sanitized padding could
/// come back unspecified (and the builder's push_small copies by
/// assignment). A byte array has no padding: copying it preserves every
/// byte.
template <typename T>
struct alignas(T) WireImage {
    std::byte bytes[sizeof(T)];
};

/// The image an inline struct writes to the buffer. A padded struct's native
/// object representation would disclose the indeterminate padding bytes and
/// make encoding nondeterministic, so its field bytes are copied one by one
/// over a zeroed image (std::bit_cast cannot build it: the result's padding
/// is unspecified). A padding-free struct's image is its whole object
/// representation.
template <typename T>
auto wire_image(const T& value) -> WireImage<T> {
    static_assert(can_inline_struct_v<T>);
    WireImage<T> image{};
    if constexpr(has_padding_v<T>) {
        copy_field_bytes(value, image.bytes);
    } else {
        std::memcpy(image.bytes, std::addressof(value), sizeof(T));
    }
    return image;
}

struct AllocFieldVisitor : detail::VisitorBase {
    builder_t& fbb;
    uoffset_t stored_offset = 0;

    bool visit_bool(bool) {
        return true;
    }

    template <typename T>
    bool visit_int(T) {
        return true;
    }

    template <typename T>
    bool visit_uint(T) {
        return true;
    }

    template <typename T>
    bool visit_float(T) {
        return true;
    }

    template <typename T>
    bool visit_char(T) {
        return true;
    }

    bool visit_null() {
        return true;
    }

    template <typename T>
    bool visit_str(const T& v) {
        std::string_view sv(v);
        auto off = fbb.CreateString(sv.data(), sv.size());
        stored_offset = off.o;
        return true;
    }

    template <typename T>
    bool visit_bytes(const T& v) {
        auto data = reinterpret_cast<const std::uint8_t*>(std::data(v));
        auto len = std::size(v);
        auto off = fbb.CreateVector(data, len);
        stored_offset = off.o;
        return true;
    }

    template <typename T, typename Body>
    bool visit_struct(const T&, Body&& body);

    template <typename Container, typename Body>
    bool visit_seq(const Container& c, Body&& body);

    template <typename T, typename Body>
    bool visit_tuple(const T&, Body&& body);

    template <typename Container, typename Body>
    bool visit_map(const Container& m, Body&& body);

    template <typename Body>
    bool visit_variant(std::size_t index, Body&& body);
};

struct AllocTableVisitor : detail::VisitorBase {
    builder_t& fbb;
    std::vector<uoffset_t> offsets{};
    std::size_t next_idx = 0;

    template <typename F>
    bool visit_field(auto index, std::string_view /*name*/, F&& writer) {
        const std::size_t I = index;
        if(offsets.size() <= I) {
            offsets.resize(I + 1, 0);
        }
        AllocFieldVisitor fv{.fbb = fbb};
        KOTA_CODEC_TRY(writer(fv));
        offsets[I] = fv.stored_offset;
        return true;
    }

    template <typename F>
    bool visit_element(F&& writer) {
        AllocFieldVisitor fv{.fbb = fbb};
        KOTA_CODEC_TRY(writer(fv));
        if(offsets.size() <= next_idx) {
            offsets.resize(next_idx + 1, 0);
        }
        offsets[next_idx] = fv.stored_offset;
        ++next_idx;
        return true;
    }
};

struct WriteFieldVisitor : detail::VisitorBase {
    builder_t& fbb;
    slot_id sid;
    uoffset_t stored_offset = 0;

    bool visit_bool(bool v) {
        fbb.AddElement<std::uint8_t>(sid, static_cast<std::uint8_t>(v));
        return true;
    }

    template <typename T>
    bool visit_int(T v) {
        fbb.AddElement<T>(sid, v);
        return true;
    }

    template <typename T>
    bool visit_uint(T v) {
        fbb.AddElement<T>(sid, v);
        return true;
    }

    template <typename T>
    bool visit_float(T v) {
        // long double fields store as double cells, matching the decode side.
        using cell_t = proxy_detail::scalar_cell_t<T>;
        fbb.AddElement<cell_t>(sid, static_cast<cell_t>(v));
        return true;
    }

    template <typename T>
    bool visit_char(T v) {
        fbb.AddElement<std::int8_t>(sid, static_cast<std::int8_t>(v));
        return true;
    }

    bool visit_null() {
        return true;
    }

    template <typename T>
    bool visit_str(const T&) {
        fbb.AddOffset(sid, offset_t<void>(stored_offset));
        return true;
    }

    template <typename T>
    bool visit_bytes(const T&) {
        fbb.AddOffset(sid, offset_t<void>(stored_offset));
        return true;
    }

    template <typename T, typename Body>
    bool visit_struct(const T& value, Body&&) {
        if constexpr(can_inline_struct_v<T>) {
            const auto image = wire_image(value);
            fbb.AddStruct(sid, &image);
        } else {
            fbb.AddOffset(sid, offset_t<void>(stored_offset));
        }
        return true;
    }

    template <typename Container, typename Body>
    bool visit_seq(const Container&, Body&&) {
        fbb.AddOffset(sid, offset_t<void>(stored_offset));
        return true;
    }

    template <typename T, typename Body>
    bool visit_tuple(const T&, Body&&) {
        fbb.AddOffset(sid, offset_t<void>(stored_offset));
        return true;
    }

    template <typename Container, typename Body>
    bool visit_map(const Container&, Body&&) {
        fbb.AddOffset(sid, offset_t<void>(stored_offset));
        return true;
    }

    template <typename Body>
    bool visit_variant(std::size_t, Body&&) {
        fbb.AddOffset(sid, offset_t<void>(stored_offset));
        return true;
    }
};

struct WriteTableVisitor : detail::VisitorBase {
    builder_t& fbb;
    const std::vector<uoffset_t>& offsets;
    std::size_t next_idx = 0;

    template <typename F>
    bool visit_field(auto index, std::string_view /*name*/, F&& writer) {
        const std::size_t I = index;
        const slot_id sid = detail::first_field + detail::field_step * static_cast<slot_id>(I);
        const auto off = (I < offsets.size()) ? offsets[I] : uoffset_t{0};
        WriteFieldVisitor wv{.fbb = fbb, .sid = sid, .stored_offset = off};
        return writer(wv);
    }

    template <typename F>
    bool visit_element(F&& writer) {
        const slot_id sid =
            detail::first_field + detail::field_step * static_cast<slot_id>(next_idx);
        const auto off = (next_idx < offsets.size()) ? offsets[next_idx] : uoffset_t{0};
        WriteFieldVisitor wv{.fbb = fbb, .sid = sid, .stored_offset = off};
        ++next_idx;
        return writer(wv);
    }
};

template <typename T>
struct ScalarElemVisitor : detail::VisitorBase {
    std::vector<T>& elems;

    bool visit_bool(bool v) {
        elems.push_back(static_cast<T>(v));
        return true;
    }

    template <typename U>
    bool visit_int(U v) {
        elems.push_back(static_cast<T>(v));
        return true;
    }

    template <typename U>
    bool visit_uint(U v) {
        elems.push_back(static_cast<T>(v));
        return true;
    }

    template <typename U>
    bool visit_float(U v) {
        elems.push_back(static_cast<T>(v));
        return true;
    }

    template <typename U>
    bool visit_char(U v) {
        elems.push_back(static_cast<T>(v));
        return true;
    }
};

template <typename T>
struct ScalarCollector {
    builder_t& fbb;
    std::vector<T> elems{};
    uoffset_t result_offset = 0;

    template <typename F>
    bool visit_element(F&& writer) {
        ScalarElemVisitor<T> ev{.elems = elems};
        return writer(ev);
    }

    bool finish() {
        auto off = fbb.CreateVector(elems.data(), elems.size());
        result_offset = off.o;
        return true;
    }
};

struct StringElemVisitor : detail::VisitorBase {
    builder_t& fbb;
    std::vector<string_offset_t>& refs;

    template <typename T>
    bool visit_str(const T& v) {
        std::string_view sv(v);
        refs.push_back(fbb.CreateString(sv.data(), sv.size()));
        return true;
    }
};

struct StringCollector {
    builder_t& fbb;
    std::vector<string_offset_t> refs{};
    uoffset_t result_offset = 0;

    template <typename F>
    bool visit_element(F&& writer) {
        StringElemVisitor ev{.fbb = fbb, .refs = refs};
        return writer(ev);
    }

    bool finish() {
        auto off = fbb.CreateVector(refs.data(), refs.size());
        result_offset = off.o;
        return true;
    }
};

template <typename T>
struct InlineStructElemVisitor : detail::VisitorBase {
    std::vector<WireImage<T>>& elems;

    // The dispatch calls visit_struct for structures. For inline structs the
    // sanitized image is simply appended to the element vector.
    template <typename U, typename Body>
    bool visit_struct(const U& value, Body&&) {
        static_assert(std::is_same_v<U, T>);
        elems.push_back(wire_image(value));
        return true;
    }
};

template <typename T>
struct InlineStructCollector {
    builder_t& fbb;
    std::vector<WireImage<T>> elems{};
    uoffset_t result_offset = 0;

    template <typename F>
    bool visit_element(F&& writer) {
        InlineStructElemVisitor<T> ev{.elems = elems};
        return writer(ev);
    }

    bool finish() {
        // The builder only memcpys the elements' bytes, but WireImage's
        // single-parameter template shape would match flatbuffers'
        // IndirectHelper<OffsetT<T>> specialization, so hand it the struct
        // type the images stand in for.
        auto off =
            fbb.CreateVectorOfStructs(reinterpret_cast<const T*>(elems.data()), elems.size());
        result_offset = off.o;
        return true;
    }
};

struct TableElemVisitor : detail::VisitorBase {
    builder_t& fbb;
    std::vector<table_offset_t>& table_offsets;

    template <typename T, typename Body>
    bool visit_struct(const T&, Body&& body);

    template <typename T, typename Body>
    bool visit_tuple(const T&, Body&& body);

    template <typename Body>
    bool visit_variant(std::size_t index, Body&& body);

    template <typename Container, typename Body>
    bool visit_seq(const Container& c, Body&& body);

    template <typename Container, typename Body>
    bool visit_map(const Container& m, Body&& body);
};

struct TableCollector {
    builder_t& fbb;
    std::vector<table_offset_t> table_offsets{};
    uoffset_t result_offset = 0;

    template <typename F>
    bool visit_element(F&& writer) {
        TableElemVisitor ev{.fbb = fbb, .table_offsets = table_offsets};
        return writer(ev);
    }

    bool finish() {
        auto off = fbb.CreateVector(table_offsets.data(), table_offsets.size());
        result_offset = off.o;
        return true;
    }
};

struct BoxedTableCollector {
    builder_t& fbb;
    std::vector<table_offset_t> table_offsets{};
    uoffset_t result_offset = 0;

    template <typename F>
    bool visit_element(F&& writer) {
        AllocFieldVisitor av{.fbb = fbb};
        KOTA_CODEC_TRY(writer(av));

        auto start = fbb.StartTable();
        WriteFieldVisitor wv{.fbb = fbb,
                             .sid = detail::first_field,
                             .stored_offset = av.stored_offset};
        KOTA_CODEC_TRY(writer(wv));
        table_offsets.push_back(table_offset_t(fbb.EndTable(start)));
        return true;
    }

    bool finish() {
        auto off = fbb.CreateVector(table_offsets.data(), table_offsets.size());
        result_offset = off.o;
        return true;
    }
};

/// The owning key an encoded map entry is ordered by, matching the comparison
/// map_view::find_entry applies to decoded keys (proxy_detail::ordering_less):
/// strings compare lexicographically (captured owning — the buffer is still
/// being built), enums by their underlying value, reflected structs
/// field-wise, every other scalar as itself.
template <typename K>
constexpr auto ordering_key_impl() {
    using clean_k = proxy_detail::deep_clean_t<K>;
    if constexpr(proxy_detail::is_string_like_v<clean_k>) {
        return std::type_identity<std::string>{};
    } else if constexpr(std::is_enum_v<clean_k>) {
        return std::type_identity<std::underlying_type_t<clean_k>>{};
    } else {
        if constexpr(meta::reflectable_class<clean_k>) {
            static_assert(can_inline_struct_v<clean_k>,
                          "a struct map key must satisfy can_inline_struct_v; a table-shaped "
                          "key has no canonical ordering for the sorted entry vector");
            // Past the reflection field limit the field walk visits nothing
            // and every key would order equal — reject instead of mis-sorting.
            static_assert(fields_reflected_v<clean_k>,
                          "this struct map key has more fields than reflection supports; its "
                          "entries cannot be ordered");
        }
        return std::type_identity<clean_k>{};
    }
}

template <typename K>
using ordering_key_t = typename decltype(ordering_key_impl<K>())::type;

/// Captures a map key as the ordering key its entry is sorted by. The events
/// mirror how the key's resolved representation encodes: scalar keys fire
/// exactly one scalar event, string keys fire visit_str, inline-struct keys
/// fire visit_struct with the whole value.
template <typename Key>
struct KeyCaptureVisitor : detail::VisitorBase {
    Key captured{};

    bool visit_bool(bool v) {
        return store(v);
    }

    template <typename T>
    bool visit_int(T v) {
        return store(v);
    }

    template <typename T>
    bool visit_uint(T v) {
        return store(v);
    }

    template <typename T>
    bool visit_float(T v) {
        return store(v);
    }

    template <typename T>
    bool visit_char(T v) {
        return store(v);
    }

    template <typename T>
    bool visit_str(const T& v) {
        captured = std::string(std::string_view(v));
        return true;
    }

    /// The ordering wants the whole value; the body's field events are not
    /// driven.
    template <typename T, typename Body>
    bool visit_struct(const T& v, Body&&) {
        captured = v;
        return true;
    }

private:
    template <typename T>
    bool store(T v) {
        captured = static_cast<Key>(v);
        return true;
    }
};

template <typename Key>
struct MapEntryCollector {
    builder_t& fbb;
    std::vector<std::pair<Key, table_offset_t>> entries{};

    template <typename KF, typename VF>
    bool visit_entry(KF&& key_fn, VF&& value_fn);
};

struct RootVisitor : detail::VisitorBase {
    builder_t& fbb;
    table_offset_t root_off{0};

    bool visit_bool(bool v) {
        return box_root_scalar<std::uint8_t>(static_cast<std::uint8_t>(v));
    }

    template <typename T>
    bool visit_int(T v) {
        return box_root_scalar<T>(v);
    }

    template <typename T>
    bool visit_uint(T v) {
        return box_root_scalar<T>(v);
    }

    template <typename T>
    bool visit_float(T v) {
        using cell_t = proxy_detail::scalar_cell_t<T>;
        return box_root_scalar<cell_t>(static_cast<cell_t>(v));
    }

    template <typename T>
    bool visit_char(T v) {
        return box_root_scalar<std::int8_t>(static_cast<std::int8_t>(v));
    }

    template <typename T>
    bool visit_str(const T& v) {
        std::string_view sv(v);
        auto str_off = fbb.CreateString(sv.data(), sv.size());
        auto start = fbb.StartTable();
        fbb.AddOffset(detail::first_field, str_off);
        root_off = table_offset_t(fbb.EndTable(start));
        return true;
    }

    template <typename T>
    bool visit_bytes(const T& v) {
        auto data = reinterpret_cast<const std::uint8_t*>(std::data(v));
        auto len = std::size(v);
        auto vec_off = fbb.CreateVector(data, len);
        auto start = fbb.StartTable();
        fbb.AddOffset(detail::first_field, vec_off);
        root_off = table_offset_t(fbb.EndTable(start));
        return true;
    }

    bool visit_null() {
        auto start = fbb.StartTable();
        root_off = table_offset_t(fbb.EndTable(start));
        return true;
    }

    template <typename T, typename Body>
    bool visit_struct(const T&, Body&& body);

    template <typename Container, typename Body>
    bool visit_seq(const Container& c, Body&& body);

    template <typename T, typename Body>
    bool visit_tuple(const T&, Body&& body);

    template <typename Container, typename Body>
    bool visit_map(const Container& m, Body&& body);

    template <typename Body>
    bool visit_variant(std::size_t index, Body&& body);

private:
    template <typename T>
    bool box_root_scalar(T v) {
        auto start = fbb.StartTable();
        fbb.AddElement<T>(detail::first_field, v);
        root_off = table_offset_t(fbb.EndTable(start));
        return true;
    }
};

/// Writes the table body() describes: one pass allocates its children, the
/// next writes its slots. A failure in either pass fails the encode; the
/// table is closed either way, so the builder stays balanced.
template <typename Body>
bool two_pass(builder_t& fbb, Body&& body, uoffset_t& out_offset) {
    AllocTableVisitor av{.fbb = fbb};
    KOTA_CODEC_TRY(body(av));

    auto start = fbb.StartTable();
    WriteTableVisitor wv{.fbb = fbb, .offsets = av.offsets};
    const bool written = body(wv);
    out_offset = fbb.EndTable(start);
    return written;
}

/// The declared key type of a map-kind container.
template <typename Container>
using map_key_t = kota::map_entry_key_t<std::ranges::range_value_t<Container>>;

template <typename Key, typename Body>
bool encode_sorted_map(builder_t& fbb, Body&& body, uoffset_t& out_offset) {
    MapEntryCollector<ordering_key_t<Key>> coll{.fbb = fbb};
    KOTA_CODEC_TRY(body(coll));

    std::sort(coll.entries.begin(), coll.entries.end(), [](const auto& a, const auto& b) {
        return proxy_detail::ordering_less(a.first, b.first);
    });

    std::vector<table_offset_t> sorted_offsets;
    sorted_offsets.reserve(coll.entries.size());
    for(auto& entry: coll.entries) {
        sorted_offsets.push_back(entry.second);
    }
    out_offset = fbb.CreateVector(sorted_offsets.data(), sorted_offsets.size()).o;
    return true;
}

template <typename Body>
bool encode_variant_table(builder_t& fbb, std::size_t index, Body&& body, uoffset_t& out_offset) {
    AllocFieldVisitor payload_alloc{.fbb = fbb};
    KOTA_CODEC_TRY(body(payload_alloc));

    const slot_id payload_slot =
        detail::first_field + detail::field_step * static_cast<slot_id>(index + 1);

    auto start = fbb.StartTable();
    fbb.AddElement<std::uint32_t>(detail::first_field, static_cast<std::uint32_t>(index));
    WriteFieldVisitor payload_write{.fbb = fbb,
                                    .sid = payload_slot,
                                    .stored_offset = payload_alloc.stored_offset};
    const bool written = body(payload_write);
    out_offset = fbb.EndTable(start);
    return written;
}

template <typename Container, typename Body>
bool seq_encode_impl(builder_t& fbb, const Container& c, Body&& body, uoffset_t& out_offset) {
    using enum proxy_detail::element_layout;
    using element_t = std::remove_cvref_t<std::ranges::range_value_t<Container>>;
    using repr_t = proxy_detail::apply_repr_t<element_t>;
    constexpr auto layout = proxy_detail::element_layout_of<element_t>();

    // A representation differing from the element type converts per element
    // in the visit chain, so the contiguous fast paths apply only when
    // elements travel as themselves.
    constexpr bool identity = std::same_as<repr_t, element_t>;
    constexpr bool contiguous =
        std::ranges::contiguous_range<Container> && std::ranges::sized_range<Container>;

    auto collect = [&](auto coll) -> bool {
        KOTA_CODEC_TRY(body(coll));
        KOTA_CODEC_TRY(coll.finish());
        out_offset = coll.result_offset;
        return true;
    };

    if constexpr(layout == scalar) {
        using cell_t = proxy_detail::scalar_cell_t<repr_t>;
        // Bitwise cell-compatible elements (the cell type itself, plus bool
        // and std::byte, whose cells share their object representation) can
        // be written straight from a contiguous range.
        constexpr bool cell_compatible = std::same_as<element_t, cell_t> ||
                                         std::same_as<element_t, std::byte> ||
                                         (std::same_as<element_t, bool> && sizeof(bool) == 1);
        if constexpr(identity && contiguous && cell_compatible) {
            auto data = reinterpret_cast<const cell_t*>(std::ranges::data(c));
            out_offset = fbb.CreateVector(data, std::ranges::size(c)).o;
            return true;
        } else {
            return collect(ScalarCollector<cell_t>{.fbb = fbb});
        }
    } else if constexpr(layout == string) {
        return collect(StringCollector{.fbb = fbb});
    } else if constexpr(layout == boxed) {
        return collect(BoxedTableCollector{.fbb = fbb});
    } else if constexpr(layout == inline_struct) {
        // A padded element's native bytes must not be copied wholesale (see
        // wire_image); such vectors build their elements one by one.
        if constexpr(identity && contiguous && !has_padding_v<repr_t>) {
            out_offset = fbb.CreateVectorOfStructs(std::ranges::data(c), std::ranges::size(c)).o;
            return true;
        } else {
            return collect(InlineStructCollector<repr_t>{.fbb = fbb});
        }
    } else {
        return collect(TableCollector{.fbb = fbb});
    }
}

template <typename T, typename Body>
bool AllocFieldVisitor::visit_struct(const T&, Body&& body) {
    if constexpr(can_inline_struct_v<T>) {
        // Inline structs are written directly by WriteFieldVisitor::visit_struct.
        // No allocation needed.
        return true;
    } else {
        detail::assert_fields_reflected<T>();
        return two_pass(fbb, std::forward<Body>(body), stored_offset);
    }
}

template <typename Container, typename Body>
bool AllocFieldVisitor::visit_seq(const Container& c, Body&& body) {
    return seq_encode_impl(fbb, c, std::forward<Body>(body), stored_offset);
}

template <typename T, typename Body>
bool AllocFieldVisitor::visit_tuple(const T&, Body&& body) {
    return two_pass(fbb, std::forward<Body>(body), stored_offset);
}

template <typename Container, typename Body>
bool AllocFieldVisitor::visit_map(const Container&, Body&& body) {
    return encode_sorted_map<map_key_t<Container>>(fbb, std::forward<Body>(body), stored_offset);
}

template <typename Body>
bool AllocFieldVisitor::visit_variant(std::size_t index, Body&& body) {
    return encode_variant_table(fbb, index, std::forward<Body>(body), stored_offset);
}

template <typename T, typename Body>
bool TableElemVisitor::visit_struct(const T&, Body&& body) {
    detail::assert_fields_reflected<T>();
    uoffset_t off = 0;
    KOTA_CODEC_TRY(two_pass(fbb, std::forward<Body>(body), off));
    table_offsets.push_back(table_offset_t(off));
    return true;
}

template <typename T, typename Body>
bool TableElemVisitor::visit_tuple(const T&, Body&& body) {
    uoffset_t off = 0;
    KOTA_CODEC_TRY(two_pass(fbb, std::forward<Body>(body), off));
    table_offsets.push_back(table_offset_t(off));
    return true;
}

template <typename Body>
bool TableElemVisitor::visit_variant(std::size_t index, Body&& body) {
    uoffset_t off = 0;
    KOTA_CODEC_TRY(encode_variant_table(fbb, index, std::forward<Body>(body), off));
    table_offsets.push_back(table_offset_t(off));
    return true;
}

template <typename Container, typename Body>
bool TableElemVisitor::visit_seq(const Container& c, Body&& body) {
    uoffset_t inner_offset = 0;
    KOTA_CODEC_TRY(seq_encode_impl(fbb, c, std::forward<Body>(body), inner_offset));

    auto start = fbb.StartTable();
    fbb.AddOffset(detail::first_field, offset_t<void>(inner_offset));
    table_offsets.push_back(table_offset_t(fbb.EndTable(start)));
    return true;
}

template <typename Container, typename Body>
bool TableElemVisitor::visit_map(const Container&, Body&& body) {
    uoffset_t vec_off = 0;
    KOTA_CODEC_TRY(encode_sorted_map<map_key_t<Container>>(fbb, std::forward<Body>(body), vec_off));

    auto start = fbb.StartTable();
    fbb.AddOffset(detail::first_field, offset_t<void>(vec_off));
    table_offsets.push_back(table_offset_t(fbb.EndTable(start)));
    return true;
}

template <typename Key>
template <typename KF, typename VF>
bool MapEntryCollector<Key>::visit_entry(KF&& key_fn, VF&& value_fn) {
    KeyCaptureVisitor<Key> capture;
    KOTA_CODEC_TRY(key_fn(capture));

    auto entry = [&](auto& sv) -> bool {
        KOTA_CODEC_TRY(sv.visit_field(std::integral_constant<std::size_t, 0>{},
                                      std::string_view{"key"},
                                      [&](auto& kv) -> bool { return key_fn(kv); }));
        return sv.visit_field(std::integral_constant<std::size_t, 1>{},
                              std::string_view{"value"},
                              [&](auto& vv) -> bool { return value_fn(vv); });
    };
    uoffset_t table_off = 0;
    KOTA_CODEC_TRY(two_pass(fbb, entry, table_off));

    entries.emplace_back(std::move(capture.captured), table_offset_t(table_off));
    return true;
}

template <typename T, typename Body>
bool RootVisitor::visit_struct(const T&, Body&& body) {
    detail::assert_fields_reflected<T>();
    uoffset_t off = 0;
    KOTA_CODEC_TRY(two_pass(fbb, std::forward<Body>(body), off));
    root_off = table_offset_t(off);
    return true;
}

template <typename Container, typename Body>
bool RootVisitor::visit_seq(const Container& c, Body&& body) {
    uoffset_t inner_offset = 0;
    KOTA_CODEC_TRY(seq_encode_impl(fbb, c, std::forward<Body>(body), inner_offset));

    auto start = fbb.StartTable();
    fbb.AddOffset(detail::first_field, offset_t<void>(inner_offset));
    root_off = table_offset_t(fbb.EndTable(start));
    return true;
}

template <typename T, typename Body>
bool RootVisitor::visit_tuple(const T&, Body&& body) {
    uoffset_t off = 0;
    KOTA_CODEC_TRY(two_pass(fbb, std::forward<Body>(body), off));
    root_off = table_offset_t(off);
    return true;
}

template <typename Container, typename Body>
bool RootVisitor::visit_map(const Container&, Body&& body) {
    uoffset_t vec_off = 0;
    KOTA_CODEC_TRY(encode_sorted_map<map_key_t<Container>>(fbb, std::forward<Body>(body), vec_off));

    auto start = fbb.StartTable();
    fbb.AddOffset(detail::first_field, offset_t<void>(vec_off));
    root_off = table_offset_t(fbb.EndTable(start));
    return true;
}

template <typename Body>
bool RootVisitor::visit_variant(std::size_t index, Body&& body) {
    uoffset_t off = 0;
    KOTA_CODEC_TRY(encode_variant_table(fbb, index, std::forward<Body>(body), off));
    root_off = table_offset_t(off);
    return true;
}

}  // namespace encode_detail

/// Encodes `value` as a finished FlatBuffers buffer (root table + "EVTO"
/// identifier), readable by from_bytes or zero-copy via table_view.
template <typename Config = void, typename T>
auto to_bytes(const T& value, std::optional<std::size_t> initial_capacity = std::nullopt)
    -> std::expected<std::vector<std::uint8_t>, rich_error> {
    detail::assert_config_layout_stable<Config>();

    builder_t fbb(initial_capacity.value_or(1024));
    encode_detail::RootVisitor vis{.fbb = fbb};
    KOTA_EXPECTED_TRY(codec::detail::run_encode<Config>(vis, value));

    fbb.Finish(vis.root_off, detail::buffer_identifier);
    const auto* begin = fbb.GetBufferPointer();
    return std::vector<std::uint8_t>(begin, begin + fbb.GetSize());
}

}  // namespace kota::codec::fbs
