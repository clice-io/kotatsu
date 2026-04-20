#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "kota/support/expected_try.h"
#include "kota/meta/schema.h"
#include "kota/codec/arena/encode.h"
#include "kota/codec/arena/traits.h"
#include "kota/codec/config.h"

namespace kota::codec::flatcode {

// ===========================================================================
// Wire format ("flatcode", a minimal flatbuffers alternative)
//
// Buffer:
//   [uint32 magic]         // 'EVFC' (0x43465645 little-endian)
//   [uint32 root_offset]   // byte offset of the root table from buffer start
//   [body: blobs packed by alignment class, descending — align-8 first]
//
// All offsets in the buffer are absolute (from buffer start). Offset 0 is a
// reserved "absent" sentinel.
//
// Table at offset T:
//   [uint32 n_slots]
//   [uint32 slot_offsets[n_slots]]   // byte offset *from T*; 0 = absent
//   [pad to max alignment of any field]
//   [field data packed in slot-id order]
//
// String at offset S:    [uint32 length][bytes[length]]          (align 4)
// Bytes  at offset S:    [uint32 length][bytes[length]]          (align 4)
// Scalar vec<T> at S:    [uint32 count][pad to alignof(T)][T[count]]
// String vec at S:       [uint32 count][uint32 string_offsets[count]]
// Table vec at S:        [uint32 count][uint32 table_offsets[count]]
// Inline-struct vec<T>:  [uint32 count][pad to alignof(T)][T[count]]
//
// slot_id == field index (0-based). variant tag at slot 0, payload alt I at
// slot I+1.
// ===========================================================================

enum class object_error_code : std::uint8_t {
    none = 0,
    invalid_state,
    unsupported_type,
    type_mismatch,
    number_out_of_range,
    too_many_fields,
};

constexpr std::string_view error_message(object_error_code code) {
    switch(code) {
        case object_error_code::none: return "none";
        case object_error_code::invalid_state: return "invalid state";
        case object_error_code::unsupported_type: return "unsupported type";
        case object_error_code::type_mismatch: return "type mismatch";
        case object_error_code::number_out_of_range: return "number out of range";
        case object_error_code::too_many_fields: return "too many fields";
    }
    return "invalid state";
}

template <typename T>
using object_result_t = std::expected<T, object_error_code>;

namespace detail {

constexpr std::uint32_t magic_evfc = 0x43465645U;  // 'EVFC'
constexpr std::uint32_t max_slot_count = 256U;     // sanity cap
constexpr std::size_t header_size = 8;             // magic + root_offset

// Strongly-typed wrappers around a blob index (index into Serializer::blobs).
// Kept distinct to catch accidental type mix-ups at compile time.
struct string_ref {
    std::uint32_t idx = 0;
};

struct vector_ref {
    std::uint32_t idx = 0;
};

struct table_ref {
    std::uint32_t idx = 0;
};

struct blob {
    std::vector<std::byte> data;
    std::uint32_t align = 4;
    std::uint32_t final_offset = 0;  // computed in bytes()
};

struct fixup {
    std::uint32_t source_blob_idx;
    std::uint32_t position;  // byte offset within source blob
    std::uint32_t target_blob_idx;
};

inline auto pad_to(std::vector<std::byte>& data, std::size_t align) -> void {
    while(data.size() % align != 0U) {
        data.push_back(std::byte{0});
    }
}

inline auto append_bytes(std::vector<std::byte>& data, const void* src, std::size_t n) -> void {
    const auto old = data.size();
    data.resize(old + n);
    std::memcpy(data.data() + old, src, n);
}

// A backend-local "inline struct" predicate independent of other backends.
// A struct is inline-able iff it is reflectable, trivial, and standard-layout
// — meaning its on-wire bytes can be a bit-copy of the in-memory layout.
template <typename T>
constexpr bool is_inline_struct_v =
    meta::reflectable_class<std::remove_cvref_t<T>> && std::is_trivial_v<std::remove_cvref_t<T>> &&
    std::is_standard_layout_v<std::remove_cvref_t<T>>;

}  // namespace detail

using detail::string_ref;
using detail::table_ref;
using detail::vector_ref;

// Arena-codec backend for the flatcode wire format.
template <typename Config = config::default_config>
class Serializer {
public:
    using config_type = Config;
    using error_type = object_error_code;
    using slot_id = std::uint32_t;
    using string_ref = detail::string_ref;
    using vector_ref = detail::vector_ref;
    using table_ref = detail::table_ref;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    template <typename T>
    constexpr static bool can_inline_struct = detail::is_inline_struct_v<T>;

    // Slot-id helpers — loop index IS the slot id in this backend.
    static auto field_slot_id(std::size_t index) -> result_t<slot_id> {
        if(index >= detail::max_slot_count) {
            return std::unexpected(object_error_code::too_many_fields);
        }
        return static_cast<slot_id>(index);
    }

    static auto variant_tag_slot_id() -> slot_id {
        return 0;
    }

    static auto variant_payload_slot_id(std::size_t index) -> result_t<slot_id> {
        return field_slot_id(index + 1);
    }

    // =======================================================================
    // TableBuilder: accumulates field operations, lays out the table on
    // finalize(). Holds per-instance operation list — each nested builder is
    // independent so construction of one does not clobber another.
    // =======================================================================
    class TableBuilder {
    public:
        explicit TableBuilder(Serializer& owner) : owner(&owner) {}

        template <typename T>
        auto add_scalar(slot_id sid, T value) -> void {
            static_assert(std::is_trivially_copyable_v<T>, "scalar must be trivially copyable");
            slot_entry e;
            e.sid = sid;
            e.align = static_cast<std::uint32_t>(alignof(T));
            e.payload.resize(sizeof(T));
            std::memcpy(e.payload.data(), std::addressof(value), sizeof(T));
            slots.push_back(std::move(e));
        }

        template <typename RefT>
        auto add_offset(slot_id sid, RefT ref) -> void {
            slot_entry e;
            e.sid = sid;
            e.align = 4;
            e.payload.resize(4);  // placeholder; filled via fixup
            e.target = ref.idx;
            slots.push_back(std::move(e));
        }

        template <typename T>
        auto add_inline_struct(slot_id sid, const T& value) -> void {
            static_assert(std::is_trivially_copyable_v<T>,
                          "inline struct must be trivially copyable");
            slot_entry e;
            e.sid = sid;
            e.align = static_cast<std::uint32_t>(alignof(T));
            e.payload.resize(sizeof(T));
            std::memcpy(e.payload.data(), std::addressof(value), sizeof(T));
            slots.push_back(std::move(e));
        }

        auto finalize() -> result_t<table_ref> {
            // Determine slot count and max alignment.
            std::uint32_t n_slots = 0;
            std::uint32_t max_align = 4;
            for(auto& s: slots) {
                n_slots = std::max<std::uint32_t>(n_slots, s.sid + 1);
                max_align = std::max<std::uint32_t>(max_align, s.align);
            }

            // Sort slots by sid for deterministic layout.
            std::sort(slots.begin(), slots.end(), [](const slot_entry& a, const slot_entry& b) {
                return a.sid < b.sid;
            });

            // Lay out the table blob.
            detail::blob b;
            b.align = max_align;

            // Header: [uint32 n_slots][uint32 slot_offsets[n_slots]]
            const std::size_t header_bytes = 4U + 4U * static_cast<std::size_t>(n_slots);
            b.data.resize(header_bytes, std::byte{0});
            std::memcpy(b.data.data(), &n_slots, 4);

            // Pad to max alignment of any field.
            detail::pad_to(b.data, max_align);

            // Temporary slot offset buffer (relative to table start).
            std::vector<std::uint32_t> slot_offsets(n_slots, 0);
            const std::uint32_t table_blob_idx = static_cast<std::uint32_t>(owner->blobs.size());
            std::vector<detail::fixup> local_fixups;

            for(auto& s: slots) {
                detail::pad_to(b.data, s.align);
                slot_offsets[s.sid] = static_cast<std::uint32_t>(b.data.size());

                if(s.target.has_value()) {
                    detail::fixup f;
                    f.source_blob_idx = table_blob_idx;
                    f.position = static_cast<std::uint32_t>(b.data.size());
                    f.target_blob_idx = *s.target;
                    local_fixups.push_back(f);
                }

                detail::append_bytes(b.data, s.payload.data(), s.payload.size());
            }

            // Write slot_offsets into the header.
            for(std::uint32_t i = 0; i < n_slots; ++i) {
                std::memcpy(b.data.data() + 4 + static_cast<std::size_t>(i) * 4,
                            &slot_offsets[i],
                            4);
            }

            owner->blobs.push_back(std::move(b));
            for(auto& f: local_fixups) {
                owner->fixups.push_back(f);
            }

            slots.clear();
            return table_ref{table_blob_idx};
        }

    private:
        struct slot_entry {
            slot_id sid = 0;
            std::uint32_t align = 4;
            std::vector<std::byte> payload;
            std::optional<std::uint32_t> target;  // blob idx if offset field
        };

        Serializer* owner;
        std::vector<slot_entry> slots;
    };

    auto start_table() -> TableBuilder {
        return TableBuilder(*this);
    }

    // =======================================================================
    // Non-table allocations — emit a blob immediately and return its ref.
    // =======================================================================

    auto alloc_string(std::string_view text) -> result_t<string_ref> {
        detail::blob b;
        b.align = 4;
        const auto len = static_cast<std::uint32_t>(text.size());
        detail::append_bytes(b.data, &len, 4);
        if(!text.empty()) {
            detail::append_bytes(b.data, text.data(), text.size());
        }
        const auto idx = static_cast<std::uint32_t>(blobs.size());
        blobs.push_back(std::move(b));
        return string_ref{idx};
    }

    auto alloc_bytes(std::span<const std::byte> bytes) -> result_t<vector_ref> {
        detail::blob b;
        b.align = 4;
        const auto len = static_cast<std::uint32_t>(bytes.size());
        detail::append_bytes(b.data, &len, 4);
        if(!bytes.empty()) {
            detail::append_bytes(b.data, bytes.data(), bytes.size());
        }
        const auto idx = static_cast<std::uint32_t>(blobs.size());
        blobs.push_back(std::move(b));
        return vector_ref{idx};
    }

    template <typename T>
    auto alloc_scalar_vector(std::span<const T> elements) -> result_t<vector_ref> {
        detail::blob b;
        b.align = std::max<std::uint32_t>(4, static_cast<std::uint32_t>(alignof(T)));
        const auto len = static_cast<std::uint32_t>(elements.size());
        detail::append_bytes(b.data, &len, 4);
        detail::pad_to(b.data, alignof(T));
        if(!elements.empty()) {
            detail::append_bytes(b.data, elements.data(), elements.size() * sizeof(T));
        }
        const auto idx = static_cast<std::uint32_t>(blobs.size());
        blobs.push_back(std::move(b));
        return vector_ref{idx};
    }

    auto alloc_string_vector(std::span<const string_ref> elements) -> result_t<vector_ref> {
        detail::blob b;
        b.align = 4;
        const auto len = static_cast<std::uint32_t>(elements.size());
        detail::append_bytes(b.data, &len, 4);
        const auto blob_idx = static_cast<std::uint32_t>(blobs.size());
        for(std::size_t i = 0; i < elements.size(); ++i) {
            detail::fixup f;
            f.source_blob_idx = blob_idx;
            f.position = static_cast<std::uint32_t>(b.data.size());
            f.target_blob_idx = elements[i].idx;
            fixups.push_back(f);

            std::uint32_t placeholder = 0;
            detail::append_bytes(b.data, &placeholder, 4);
        }
        blobs.push_back(std::move(b));
        return vector_ref{blob_idx};
    }

    template <typename T>
    auto alloc_inline_struct_vector(std::span<const T> elements) -> result_t<vector_ref> {
        detail::blob b;
        b.align = std::max<std::uint32_t>(4, static_cast<std::uint32_t>(alignof(T)));
        const auto len = static_cast<std::uint32_t>(elements.size());
        detail::append_bytes(b.data, &len, 4);
        detail::pad_to(b.data, alignof(T));
        if(!elements.empty()) {
            detail::append_bytes(b.data, elements.data(), elements.size() * sizeof(T));
        }
        const auto idx = static_cast<std::uint32_t>(blobs.size());
        blobs.push_back(std::move(b));
        return vector_ref{idx};
    }

    auto alloc_table_vector(std::span<const table_ref> elements) -> result_t<vector_ref> {
        detail::blob b;
        b.align = 4;
        const auto len = static_cast<std::uint32_t>(elements.size());
        detail::append_bytes(b.data, &len, 4);
        const auto blob_idx = static_cast<std::uint32_t>(blobs.size());
        for(std::size_t i = 0; i < elements.size(); ++i) {
            detail::fixup f;
            f.source_blob_idx = blob_idx;
            f.position = static_cast<std::uint32_t>(b.data.size());
            f.target_blob_idx = elements[i].idx;
            fixups.push_back(f);

            std::uint32_t placeholder = 0;
            detail::append_bytes(b.data, &placeholder, 4);
        }
        blobs.push_back(std::move(b));
        return vector_ref{blob_idx};
    }

    // =======================================================================
    // Finishing — compute absolute offsets, apply fixups, emit final buffer.
    // =======================================================================

    auto finish(table_ref root) -> result_t<void> {
        root_blob = root.idx;
        finished = true;
        return {};
    }

    auto bytes() -> std::vector<std::uint8_t> {
        if(!finished) {
            return {};
        }

        // Bucket blobs by alignment class: 8 / 4 / 2 / 1.
        std::vector<std::uint32_t> buckets[4];
        auto bucket_of = [](std::uint32_t align) -> std::size_t {
            if(align >= 8)
                return 0;
            if(align >= 4)
                return 1;
            if(align >= 2)
                return 2;
            return 3;
        };
        for(std::uint32_t i = 0; i < blobs.size(); ++i) {
            buckets[bucket_of(blobs[i].align)].push_back(i);
        }

        // Compute final offset for each blob. Re-align before every blob: the
        // previous blob's size may not be a multiple of its alignment (e.g., a
        // 5-byte string in the align-4 bucket), so same-bucket neighbours still
        // need padding between them.
        std::size_t cursor = detail::header_size;
        constexpr std::uint32_t bucket_aligns[4] = {8, 4, 2, 1};
        for(std::size_t bi = 0; bi < 4; ++bi) {
            for(auto idx: buckets[bi]) {
                const auto a = blobs[idx].align;
                while(cursor % a != 0U) {
                    ++cursor;
                }
                blobs[idx].final_offset = static_cast<std::uint32_t>(cursor);
                cursor += blobs[idx].data.size();
            }
            // Also ensure the cursor is at least aligned to the *next* bucket's
            // alignment — this is implicitly guaranteed because bucket_aligns
            // is descending, but kept here for clarity.
            (void)bucket_aligns;
        }

        // Apply fixups.
        for(auto& f: fixups) {
            auto& src = blobs[f.source_blob_idx];
            const auto target_offset = blobs[f.target_blob_idx].final_offset;
            std::memcpy(src.data.data() + f.position, &target_offset, 4);
        }

        // Emit final buffer.
        std::vector<std::uint8_t> out;
        out.reserve(cursor);

        // Header: magic + root_offset.
        out.resize(detail::header_size);
        const std::uint32_t magic = detail::magic_evfc;
        std::memcpy(out.data(), &magic, 4);
        const std::uint32_t root_offset = blobs[root_blob].final_offset;
        std::memcpy(out.data() + 4, &root_offset, 4);

        // Blobs, bucket-ordered — mirror the layout logic above, padding
        // before each blob to match its individual alignment.
        for(std::size_t bi = 0; bi < 4; ++bi) {
            for(auto idx: buckets[bi]) {
                const auto& b = blobs[idx];
                while(out.size() % b.align != 0U) {
                    out.push_back(0);
                }
                out.insert(out.end(),
                           reinterpret_cast<const std::uint8_t*>(b.data.data()),
                           reinterpret_cast<const std::uint8_t*>(b.data.data() + b.data.size()));
            }
        }

        return out;
    }

    // Top-level entry.
    template <typename T>
    auto bytes(const T& value) -> result_t<std::vector<std::uint8_t>> {
        reset();
        KOTA_EXPECTED_TRY_V(auto root, (arena::encode_root<Config>(*this, value)));
        KOTA_EXPECTED_TRY(finish(root));
        return bytes();
    }

private:
    auto reset() -> void {
        blobs.clear();
        fixups.clear();
        root_blob = 0;
        finished = false;
    }

    std::vector<detail::blob> blobs;
    std::vector<detail::fixup> fixups;
    std::uint32_t root_blob = 0;
    bool finished = false;
};

template <typename Config = config::default_config, typename T>
auto to_flatcode(const T& value) -> object_result_t<std::vector<std::uint8_t>> {
    Serializer<Config> serializer;
    return serializer.bytes(value);
}

}  // namespace kota::codec::flatcode
