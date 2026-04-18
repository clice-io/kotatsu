#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "kota/support/expected_try.h"
#include "kota/support/ranges.h"
#include "kota/support/type_list.h"
#include "kota/codec/config.h"
#include "kota/codec/traits.h"
#include "kota/codec/detail/common.h"
#include "kota/codec/flatbuffers/schema.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"

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

using codec::detail::clean_t;
using codec::detail::remove_annotation_t;
using codec::detail::remove_optional_t;

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

inline auto variant_field_voffset(std::size_t index) -> object_result_t<::flatbuffers::voffset_t> {
    return field_voffset(index + 1);
}

// Pick a slot's `tagged` schema attribute, or void if none.
template <typename AttrsTuple>
using slot_tagged_attr_t = meta::detail::tagged_schema_attr_t<AttrsTuple>;

template <typename AttrsTuple>
constexpr bool has_slot_tagged_attr_v = meta::detail::has_tagged_schema_attr_v<AttrsTuple>;

}  // namespace detail

template <typename Config = config::default_config>
class Serializer {
public:
    using config_type = Config;
    using value_type = ::flatbuffers::Offset<::flatbuffers::Table>;
    using error_type = object_error_code;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    using status_t = result_t<void>;

    explicit Serializer(std::size_t initial_capacity = 1024) : builder(initial_capacity) {}

    template <typename T>
    auto bytes(const T& value) -> result_t<std::vector<std::uint8_t>> {
        builder.Clear();
        KOTA_EXPECTED_TRY_V(auto root, encode_root(value));
        builder.Finish(root, detail::buffer_identifier);
        const auto* begin = builder.GetBufferPointer();
        return std::vector<std::uint8_t>(begin, begin + builder.GetSize());
    }

private:
    using writer_t = std::function<void()>;
    using writer_list = std::vector<writer_t>;

    // Non-template helper to avoid per-instantiation lambda vtable churn.
    void push_add_offset(writer_list& writers,
                         ::flatbuffers::voffset_t field,
                         ::flatbuffers::uoffset_t raw_offset) {
        writers.push_back([this, field, raw_offset] {
            builder.AddOffset(field, ::flatbuffers::Offset<void>(raw_offset));
        });
    }

    template <typename OffsetT>
    auto wrap_offset(::flatbuffers::Offset<OffsetT> offset) -> result_t<value_type> {
        writer_list writers;
        push_add_offset(writers, detail::first_field, offset.o);
        return finish_table(writers);
    }

    auto finish_table(writer_list& writers) -> result_t<value_type> {
        const auto start = builder.StartTable();
        for(auto& write: writers) {
            write();
        }
        return value_type(builder.EndTable(start));
    }

    // === Root encoding =====================================================
    template <typename T>
    auto encode_root(const T& value) -> result_t<value_type> {
        using U = std::remove_cvref_t<T>;

        if constexpr(meta::annotated_type<U>) {
            return encode_root(meta::annotated_value(value));
        } else if constexpr(is_specialization_of<std::optional, U>) {
            if(!value.has_value()) {
                return encode_boxed(value);
            }
            return encode_root(*value);
        } else if constexpr(meta::reflectable_class<U> && !can_inline_struct_v<U> &&
                            !std::ranges::input_range<U> && !is_pair_v<U> && !is_tuple_v<U>) {
            return encode_struct<U>(value);
        } else if constexpr(is_specialization_of<std::variant, U>) {
            return encode_variant<std::tuple<>>(value);
        } else if constexpr(is_pair_v<U> || is_tuple_v<U>) {
            return encode_tuple_like(value);
        } else {
            return encode_boxed(value);
        }
    }

    template <typename T>
    auto encode_boxed(const T& value) -> result_t<value_type> {
        writer_list writers;
        KOTA_EXPECTED_TRY(
            (collect_field<std::remove_cvref_t<T>, std::tuple<>>(writers, detail::first_field, value)));
        return finish_table(writers);
    }

    // === Struct encoding via virtual_schema ================================
    template <typename T>
    auto encode_struct(const T& value) -> result_t<value_type> {
        using schema = meta::virtual_schema<T, Config>;
        using slots = typename schema::slots;
        constexpr std::size_t N = kota::type_list_size_v<slots>;

        writer_list writers;
        if constexpr(N > 0) {
            writers.reserve(N);
        }

        status_t status{};
        bool ok = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return (this->template encode_struct_slot<T, Is>(value, writers, status) && ...);
        }(std::make_index_sequence<N>{});

        if(!ok) {
            return std::unexpected(status.error());
        }
        return finish_table(writers);
    }

    template <typename T, std::size_t I>
    auto encode_struct_slot(const T& value, writer_list& writers, status_t& status) -> bool {
        using schema = meta::virtual_schema<T, Config>;
        using slot_t = kota::type_list_element_t<I, typename schema::slots>;
        using raw_t = typename slot_t::raw_type;
        using attrs_t = typename slot_t::attrs;

        constexpr std::size_t offset = schema::fields[I].offset;
        constexpr std::size_t physical_index = schema::fields[I].physical_index;
        auto const* base = reinterpret_cast<const std::byte*>(std::addressof(value));
        auto const& field_value = *reinterpret_cast<const raw_t*>(base + offset);

        // skip_if short-circuit before we compute a voffset.
        if constexpr(kota::tuple_has_spec_v<attrs_t, meta::behavior::skip_if>) {
            using pred =
                typename kota::tuple_find_spec_t<attrs_t, meta::behavior::skip_if>::predicate;
            if(meta::evaluate_skip_predicate<pred>(field_value, /*is_serialize=*/true)) {
                return true;
            }
        }

        // Use physical_index so `skip` leaves a hole in the voffset layout
        // (preserves the pointer-to-member→voffset mapping used by proxy views).
        auto voffset = detail::field_voffset(physical_index);
        if(!voffset) {
            status = std::unexpected(voffset.error());
            return false;
        }

        auto r = collect_field<raw_t, attrs_t>(writers, *voffset, field_value);
        if(!r) {
            status = std::unexpected(r.error());
            return false;
        }
        return true;
    }

    // === Field-level entry (applies behavior attrs, then dispatches) =======
    template <typename Raw, typename Attrs, typename V>
    auto collect_field(writer_list& writers, ::flatbuffers::voffset_t voffset, const V& value)
        -> status_t {
        using U = std::remove_cvref_t<V>;

        // Strip annotation transparently; attrs from the slot already apply.
        if constexpr(meta::annotated_type<U>) {
            using inner = typename U::annotated_type;
            return collect_field<inner, Attrs>(writers, voffset, meta::annotated_value(value));
        } else if constexpr(kota::tuple_has_spec_v<Attrs, meta::behavior::as>) {
            using target = typename kota::tuple_find_spec_t<Attrs, meta::behavior::as>::target;
            target tmp = static_cast<target>(value);
            return collect_field<target, std::tuple<>>(writers, voffset, tmp);
        } else if constexpr(kota::tuple_has_spec_v<Attrs, meta::behavior::enum_string>) {
            using clean_u = detail::clean_t<U>;
            static_assert(std::is_enum_v<clean_u>, "enum_string requires an enum type");
            std::string_view name = meta::enum_name(static_cast<clean_u>(value));
            const auto offset = builder.CreateString(name.data(), name.size());
            push_add_offset(writers, voffset, offset.o);
            return {};
        } else if constexpr(kota::tuple_has_spec_v<Attrs, meta::behavior::with>) {
            using adapter =
                typename kota::tuple_find_spec_t<Attrs, meta::behavior::with>::adapter;
            using wire_t = typename adapter::wire_type;
            wire_t wire = adapter::to_wire(value);
            return collect_field<wire_t, std::tuple<>>(writers, voffset, wire);
        } else {
            return dispatch_field<Raw, Attrs>(writers, voffset, value);
        }
    }

    // === Value dispatch (attrs only read for variant tagging) ==============
    template <typename Raw, typename Attrs, typename V>
    auto dispatch_field(writer_list& writers, ::flatbuffers::voffset_t voffset, const V& value)
        -> status_t {
        using U = std::remove_cvref_t<V>;
        using clean_u = detail::clean_t<U>;

        if constexpr(is_specialization_of<std::optional, U>) {
            if(!value.has_value()) {
                return {};
            }
            using inner = typename U::value_type;
            return collect_field<inner, std::tuple<>>(writers, voffset, *value);
        } else if constexpr(is_specialization_of<std::unique_ptr, U> ||
                            is_specialization_of<std::shared_ptr, U>) {
            if(!value) {
                return {};
            }
            using inner = typename U::element_type;
            return collect_field<inner, std::tuple<>>(writers, voffset, *value);
        } else if constexpr(std::same_as<clean_u, std::nullptr_t>) {
            return {};
        } else if constexpr(std::is_enum_v<clean_u>) {
            using underlying = std::underlying_type_t<clean_u>;
            return collect_field<underlying, std::tuple<>>(
                writers, voffset, static_cast<underlying>(value));
        } else if constexpr(codec::bool_like<clean_u>) {
            const bool v = static_cast<bool>(value);
            writers.push_back([this, voffset, v] { builder.AddElement<bool>(voffset, v); });
            return {};
        } else if constexpr(codec::int_like<clean_u>) {
            const clean_u v = static_cast<clean_u>(value);
            writers.push_back([this, voffset, v] { builder.AddElement<clean_u>(voffset, v); });
            return {};
        } else if constexpr(codec::uint_like<clean_u>) {
            const clean_u v = static_cast<clean_u>(value);
            writers.push_back([this, voffset, v] { builder.AddElement<clean_u>(voffset, v); });
            return {};
        } else if constexpr(codec::floating_like<clean_u>) {
            if constexpr(std::same_as<clean_u, float> || std::same_as<clean_u, double>) {
                const clean_u v = static_cast<clean_u>(value);
                writers.push_back([this, voffset, v] { builder.AddElement<clean_u>(voffset, v); });
            } else {
                const double v = static_cast<double>(value);
                writers.push_back([this, voffset, v] { builder.AddElement<double>(voffset, v); });
            }
            return {};
        } else if constexpr(codec::char_like<clean_u>) {
            const std::int8_t v = static_cast<std::int8_t>(value);
            writers.push_back(
                [this, voffset, v] { builder.AddElement<std::int8_t>(voffset, v); });
            return {};
        } else if constexpr(codec::str_like<clean_u>) {
            const std::string_view text = value;
            const auto offset = builder.CreateString(text.data(), text.size());
            push_add_offset(writers, voffset, offset.o);
            return {};
        } else if constexpr(codec::bytes_like<clean_u>) {
            const std::span<const std::byte> bytes = value;
            const auto* data =
                bytes.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(bytes.data());
            const auto offset = builder.CreateVector(data, bytes.size());
            push_add_offset(writers, voffset, offset.o);
            return {};
        } else if constexpr(is_specialization_of<std::variant, U>) {
            KOTA_EXPECTED_TRY_V(auto offset, (encode_variant<Attrs>(value)));
            push_add_offset(writers, voffset, offset.o);
            return {};
        } else if constexpr(std::ranges::input_range<clean_u>) {
            constexpr auto kind = kota::format_kind<clean_u>;
            if constexpr(kind == kota::range_format::map) {
                KOTA_EXPECTED_TRY_V(auto offset, encode_map(value));
                push_add_offset(writers, voffset, offset.o);
                return {};
            } else {
                return collect_sequence_field(writers, voffset, value);
            }
        } else if constexpr(is_pair_v<clean_u> || is_tuple_v<clean_u>) {
            KOTA_EXPECTED_TRY_V(auto offset, encode_tuple_like(value));
            push_add_offset(writers, voffset, offset.o);
            return {};
        } else if constexpr(can_inline_struct_v<clean_u>) {
            const clean_u copy = static_cast<clean_u>(value);
            writers.push_back([this, voffset, copy] { builder.AddStruct(voffset, &copy); });
            return {};
        } else if constexpr(meta::reflectable_class<clean_u>) {
            KOTA_EXPECTED_TRY_V(auto offset, encode_struct<clean_u>(value));
            push_add_offset(writers, voffset, offset.o);
            return {};
        } else {
            return std::unexpected(object_error_code::unsupported_type);
        }
    }

    // === Sequence ==========================================================
    template <typename T>
    auto collect_sequence_field(writer_list& writers,
                                ::flatbuffers::voffset_t voffset,
                                const T& value) -> status_t {
        using U = std::remove_cvref_t<T>;
        using element_t = std::ranges::range_value_t<U>;
        using element_clean_t = detail::clean_t<element_t>;

        if constexpr(std::same_as<element_clean_t, std::byte>) {
            std::vector<std::uint8_t> bytes;
            if constexpr(requires { value.size(); }) {
                bytes.reserve(value.size());
            }
            for(auto b: value) {
                bytes.push_back(std::to_integer<std::uint8_t>(b));
            }
            const auto offset = builder.CreateVector(bytes);
            push_add_offset(writers, voffset, offset.o);
            return {};
        } else if constexpr(codec::bool_like<element_clean_t> ||
                            codec::int_like<element_clean_t> ||
                            codec::uint_like<element_clean_t>) {
            std::vector<element_clean_t> elements;
            if constexpr(requires { value.size(); }) {
                elements.reserve(value.size());
            }
            for(const auto& e: value) {
                elements.push_back(static_cast<element_clean_t>(e));
            }
            const auto offset = builder.CreateVector(elements);
            push_add_offset(writers, voffset, offset.o);
            return {};
        } else if constexpr(codec::floating_like<element_clean_t>) {
            if constexpr(std::same_as<element_clean_t, float> ||
                         std::same_as<element_clean_t, double>) {
                std::vector<element_clean_t> elements;
                if constexpr(requires { value.size(); }) {
                    elements.reserve(value.size());
                }
                for(const auto& e: value) {
                    elements.push_back(static_cast<element_clean_t>(e));
                }
                const auto offset = builder.CreateVector(elements);
                push_add_offset(writers, voffset, offset.o);
                return {};
            } else {
                std::vector<double> elements;
                if constexpr(requires { value.size(); }) {
                    elements.reserve(value.size());
                }
                for(const auto& e: value) {
                    elements.push_back(static_cast<double>(e));
                }
                const auto offset = builder.CreateVector(elements);
                push_add_offset(writers, voffset, offset.o);
                return {};
            }
        } else if constexpr(codec::char_like<element_clean_t>) {
            std::vector<std::int8_t> elements;
            if constexpr(requires { value.size(); }) {
                elements.reserve(value.size());
            }
            for(const auto& e: value) {
                elements.push_back(static_cast<std::int8_t>(e));
            }
            const auto offset = builder.CreateVector(elements);
            push_add_offset(writers, voffset, offset.o);
            return {};
        } else if constexpr(codec::str_like<element_clean_t>) {
            std::vector<::flatbuffers::Offset<::flatbuffers::String>> elements;
            if constexpr(requires { value.size(); }) {
                elements.reserve(value.size());
            }
            for(const auto& e: value) {
                const std::string_view text = e;
                elements.push_back(builder.CreateString(text.data(), text.size()));
            }
            const auto offset = builder.CreateVector(elements);
            push_add_offset(writers, voffset, offset.o);
            return {};
        } else if constexpr(is_pair_v<element_clean_t> || is_tuple_v<element_clean_t>) {
            std::vector<value_type> elements;
            if constexpr(requires { value.size(); }) {
                elements.reserve(value.size());
            }
            for(const auto& e: value) {
                KOTA_EXPECTED_TRY_V(auto t, encode_tuple_like(e));
                elements.push_back(t);
            }
            const auto offset = builder.CreateVector(elements);
            push_add_offset(writers, voffset, offset.o);
            return {};
        } else if constexpr(can_inline_struct_v<element_clean_t>) {
            std::vector<element_clean_t> elements;
            if constexpr(requires { value.size(); }) {
                elements.reserve(value.size());
            }
            for(const auto& e: value) {
                elements.push_back(static_cast<element_clean_t>(e));
            }
            const auto offset = builder.CreateVectorOfStructs(elements);
            push_add_offset(writers, voffset, offset.o);
            return {};
        } else if constexpr(meta::reflectable_class<element_clean_t>) {
            std::vector<value_type> elements;
            if constexpr(requires { value.size(); }) {
                elements.reserve(value.size());
            }
            for(const auto& e: value) {
                KOTA_EXPECTED_TRY_V(auto t, encode_struct<element_clean_t>(e));
                elements.push_back(t);
            }
            const auto offset = builder.CreateVector(elements);
            push_add_offset(writers, voffset, offset.o);
            return {};
        } else {
            std::vector<value_type> elements;
            if constexpr(requires { value.size(); }) {
                elements.reserve(value.size());
            }
            for(const auto& e: value) {
                KOTA_EXPECTED_TRY_V(auto t, encode_boxed(e));
                elements.push_back(t);
            }
            const auto offset = builder.CreateVector(elements);
            push_add_offset(writers, voffset, offset.o);
            return {};
        }
    }

    // === Map ===============================================================
    template <typename T>
    auto encode_map(const T& value) -> result_t<
        ::flatbuffers::Offset<::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::Table>>>> {
        using U = std::remove_cvref_t<T>;
        using key_t = typename U::key_type;
        using mapped_t = typename U::mapped_type;

        std::vector<std::pair<key_t, mapped_t>> entries;
        entries.reserve(value.size());
        for(const auto& [k, v]: value) {
            entries.emplace_back(k, v);
        }
        if constexpr(requires(const key_t& a, const key_t& b) {
                         { a < b } -> std::convertible_to<bool>;
                     }) {
            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
                return a.first < b.first;
            });
        }

        std::vector<value_type> offsets;
        offsets.reserve(entries.size());
        for(const auto& [k, v]: entries) {
            writer_list writers;
            KOTA_EXPECTED_TRY((collect_field<key_t, std::tuple<>>(writers, detail::first_field, k)));
            KOTA_EXPECTED_TRY_V(auto value_field, detail::field_voffset(1));
            KOTA_EXPECTED_TRY((collect_field<mapped_t, std::tuple<>>(writers, value_field, v)));
            KOTA_EXPECTED_TRY_V(auto entry, finish_table(writers));
            offsets.push_back(entry);
        }
        return builder.CreateVector(offsets);
    }

    // === Tuple / pair =======================================================
    template <typename T>
    auto encode_tuple_like(const T& value) -> result_t<value_type> {
        using U = std::remove_cvref_t<T>;

        writer_list writers;
        status_t status{};

        bool ok = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return (this->template collect_tuple_element<U, Is>(writers, status, value) && ...);
        }(std::make_index_sequence<std::tuple_size_v<U>>{});
        if(!ok) {
            return std::unexpected(status.error());
        }
        return finish_table(writers);
    }

    template <typename Tuple, std::size_t I, typename V>
    auto collect_tuple_element(writer_list& writers, status_t& status, const V& tup) -> bool {
        auto vo = detail::field_voffset(I);
        if(!vo) {
            status = std::unexpected(vo.error());
            return false;
        }
        using element_t = std::tuple_element_t<I, Tuple>;
        auto r = collect_field<element_t, std::tuple<>>(writers, *vo, std::get<I>(tup));
        if(!r) {
            status = std::unexpected(r.error());
            return false;
        }
        return true;
    }

    // === Variant ===========================================================
    template <typename Attrs, typename T>
    auto encode_variant(const T& value) -> result_t<value_type> {
        using U = std::remove_cvref_t<T>;
        static_assert(is_specialization_of<std::variant, U>, "variant required");

        writer_list writers;
        const auto variant_index = static_cast<std::uint32_t>(value.index());
        writers.push_back([this, variant_index] {
            builder.AddElement<std::uint32_t>(detail::first_field, variant_index);
        });

        status_t status{};
        bool matched = false;
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            (
                [&] {
                    if(value.index() != Is) {
                        return;
                    }
                    matched = true;
                    auto vo = detail::variant_field_voffset(Is);
                    if(!vo) {
                        status = std::unexpected(vo.error());
                        return;
                    }
                    using alt_t = std::variant_alternative_t<Is, U>;
                    auto r = collect_field<alt_t, std::tuple<>>(writers, *vo, std::get<Is>(value));
                    if(!r) {
                        status = std::unexpected(r.error());
                    }
                }(),
                ...);
        }(std::make_index_sequence<std::variant_size_v<U>>{});

        if(!matched) {
            return std::unexpected(object_error_code::invalid_state);
        }
        if(!status) {
            return std::unexpected(status.error());
        }
        return finish_table(writers);
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
