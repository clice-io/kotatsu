#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "kota/codec/config.h"
#include "kota/codec/detail/arena_encode.h"
#include "kota/codec/detail/arena_traits.h"
#include "kota/codec/flatbuffers/struct_layout.h"

#if __has_include(<flatbuffers/flatbuffers.h>)
#include "flatbuffers/flatbuffers.h"
#else
#error                                                                                             \
    "flatbuffers/flatbuffers.h not found. Enable KOTA_CODEC_ENABLE_FLATBUFFERS or add flatbuffers include paths."
#endif

namespace kota::codec::flatbuffers {

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

constexpr inline char buffer_identifier[] = "EVTO";
constexpr ::flatbuffers::voffset_t first_field = 4;
constexpr ::flatbuffers::voffset_t field_step = 2;

inline auto field_voffset(std::size_t index) -> object_result_t<::flatbuffers::voffset_t> {
    constexpr auto max_voffset =
        static_cast<std::size_t>((std::numeric_limits<::flatbuffers::voffset_t>::max)());
    const auto raw =
        static_cast<std::size_t>(first_field) + index * static_cast<std::size_t>(field_step);
    if(raw > max_voffset) {
        return std::unexpected(object_error_code::too_many_fields);
    }
    return static_cast<::flatbuffers::voffset_t>(raw);
}

inline auto variant_payload_voffset(std::size_t index)
    -> object_result_t<::flatbuffers::voffset_t> {
    return field_voffset(index + 1);
}

}  // namespace detail

// Arena-codec backend for flatbuffers.
//
// Exposes the arena serializer trait on top of a ::flatbuffers::FlatBufferBuilder.
// All type-dispatch logic lives in kota::codec::arena::encode_* — this class is
// a thin adapter that turns arena primitives into FlatBufferBuilder calls.
//
// NOTE: FlatBufferBuilder forbids calling CreateString / CreateVector while a
// table is open. Each TableBuilder therefore DEFERS every add_* call into its
// own writer list and flushes it within StartTable / EndTable inside
// finalize(). Nested TableBuilders hold their own lists so they can be built
// concurrently with an outer builder without clobbering its queued writes.
template <typename Config = config::default_config>
class Serializer {
public:
    using config_type = Config;
    using error_type = object_error_code;
    using slot_id = ::flatbuffers::voffset_t;
    using string_ref = ::flatbuffers::Offset<::flatbuffers::String>;
    using vector_ref = ::flatbuffers::uoffset_t;  // type-erased vector offset
    using table_ref = ::flatbuffers::Offset<::flatbuffers::Table>;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    // FlatBuffers supports inline structs both as table fields (CreateStruct
    // in a table slot) and as list elements (CreateVectorOfStructs), so both
    // predicates resolve to the same underlying trait.
    template <typename T>
    constexpr static bool can_inline_struct_field = flatbuffers::can_inline_struct_v<T>;

    template <typename T>
    constexpr static bool can_inline_struct_element = flatbuffers::can_inline_struct_v<T>;

    // Slot-id helpers: derive an on-wire slot id from a loop index.
    static auto field_slot_id(std::size_t index) -> result_t<slot_id> {
        return detail::field_voffset(index);
    }

    static auto variant_tag_slot_id() -> slot_id {
        return detail::first_field;
    }

    static auto variant_payload_slot_id(std::size_t index) -> result_t<slot_id> {
        return detail::variant_payload_voffset(index);
    }

    explicit Serializer(std::size_t initial_capacity = 1024) : builder(initial_capacity) {}

    // === Arena primitives =================================================

    class TableBuilder {
    public:
        explicit TableBuilder(Serializer& owner) : owner(&owner) {}

        template <typename T>
        auto add_scalar(slot_id sid, T value) -> void {
            writers.push_back([b = &owner->builder, sid, value] { b->AddElement<T>(sid, value); });
        }

        template <typename RefT>
        auto add_offset(slot_id sid, RefT ref) -> void {
            const auto raw_offset = raw_uoffset(ref);
            writers.push_back([b = &owner->builder, sid, raw_offset] {
                b->AddOffset(sid, ::flatbuffers::Offset<void>(raw_offset));
            });
        }

        template <typename T>
        auto add_inline_struct(slot_id sid, const T& value) -> void {
            writers.push_back(
                [b = &owner->builder, sid, copy = value] { b->AddStruct(sid, &copy); });
        }

        auto finalize() -> result_t<table_ref> {
            const auto start = owner->builder.StartTable();
            for(auto& write: writers) {
                write();
            }
            writers.clear();
            return table_ref(owner->builder.EndTable(start));
        }

    private:
        static auto raw_uoffset(::flatbuffers::uoffset_t raw) -> ::flatbuffers::uoffset_t {
            return raw;
        }

        template <typename OffsetT>
        static auto raw_uoffset(::flatbuffers::Offset<OffsetT> offset) -> ::flatbuffers::uoffset_t {
            return offset.o;
        }

        Serializer* owner;
        std::vector<std::function<void()>> writers;
    };

    auto start_table() -> TableBuilder {
        return TableBuilder(*this);
    }

    auto alloc_string(std::string_view text) -> result_t<string_ref> {
        return builder.CreateString(text.data(), text.size());
    }

    auto alloc_bytes(std::span<const std::byte> bytes) -> result_t<vector_ref> {
        const auto* data =
            bytes.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(bytes.data());
        return builder.CreateVector(data, bytes.size()).o;
    }

    template <typename T>
    auto alloc_scalar_vector(std::span<const T> elements) -> result_t<vector_ref> {
        return builder.CreateVector(elements.data(), elements.size()).o;
    }

    auto alloc_string_vector(std::span<const string_ref> elements) -> result_t<vector_ref> {
        return builder.CreateVector(elements.data(), elements.size()).o;
    }

    template <typename T>
    auto alloc_inline_struct_vector(std::span<const T> elements) -> result_t<vector_ref> {
        return builder.CreateVectorOfStructs(elements.data(), elements.size()).o;
    }

    auto alloc_table_vector(std::span<const table_ref> elements) -> result_t<vector_ref> {
        return builder.CreateVector(elements.data(), elements.size()).o;
    }

    auto finish(table_ref root) -> result_t<void> {
        builder.Finish(root, detail::buffer_identifier);
        return {};
    }

    auto bytes() -> std::vector<std::uint8_t> {
        const auto* begin = builder.GetBufferPointer();
        return std::vector<std::uint8_t>(begin, begin + builder.GetSize());
    }

    // === Top-level entry (public) =========================================
    template <typename T>
    auto bytes(const T& value) -> result_t<std::vector<std::uint8_t>> {
        builder.Clear();
        KOTA_EXPECTED_TRY_V(auto root, (arena::encode_root<Config>(*this, value)));
        KOTA_EXPECTED_TRY(finish(root));
        return bytes();
    }

private:
    ::flatbuffers::FlatBufferBuilder builder;
};

template <typename Config = config::default_config, typename T>
auto to_flatbuffer(const T& value, std::optional<std::size_t> initial_capacity = std::nullopt)
    -> object_result_t<std::vector<std::uint8_t>> {
    Serializer<Config> serializer(initial_capacity.value_or(1024));
    return serializer.bytes(value);
}

}  // namespace kota::codec::flatbuffers
