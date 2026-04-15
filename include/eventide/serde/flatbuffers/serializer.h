#pragma once

#include <algorithm>
#include <concepts>
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

#include "eventide/common/expected_try.h"
#include "eventide/common/ranges.h"
#include "eventide/serde/flatbuffers/schema.h"
#include "eventide/serde/serde/config.h"
#include "eventide/serde/serde/serde.h"
#include "eventide/serde/serde/utils/common.h"

#if __has_include(<flatbuffers/flatbuffers.h>)
#include <flatbuffers/flatbuffers.h>
#else
#error                                                                                             \
    "flatbuffers/flatbuffers.h not found. Enable ETD_SERDE_ENABLE_FLATBUFFERS or add flatbuffers include paths."
#endif

namespace eventide::serde::flatbuffers {

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

using serde::detail::clean_t;
using serde::detail::remove_annotation_t;
using serde::detail::remove_optional_t;

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

    class SerializeSeq {
    public:
        explicit SerializeSeq(Serializer& serializer, std::optional<std::size_t> len) noexcept :
            serializer(serializer) {
            if(len.has_value()) {
                elements.reserve(*len);
            }
        }

        template <typename T>
        status_t serialize_element(const T& value) {
            ETD_EXPECTED_TRY_V(auto encoded, serde::serialize(serializer, value));
            elements.push_back(encoded);
            return {};
        }

        result_t<value_type> end() {
            const auto offset = serializer.builder.CreateVector(elements);
            return serializer.wrap_offset(offset);
        }

    private:
        Serializer& serializer;
        std::vector<value_type> elements;
    };

    class SerializeTuple {
    public:
        explicit SerializeTuple(Serializer& serializer, std::size_t len) noexcept :
            serializer(serializer) {
            writers.reserve(len);
        }

        template <typename T>
        status_t serialize_element(const T& value) {
            ETD_EXPECTED_TRY_V(auto field_id, detail::field_voffset(next_index));
            ++next_index;

            return serializer.collect_field(writers, field_id, value);
        }

        result_t<value_type> end() {
            return serializer.finish_table(writers);
        }

    private:
        Serializer& serializer;
        std::vector<std::function<void()>> writers;
        std::size_t next_index = 0;
    };

    class SerializeMap {
    public:
        explicit SerializeMap(Serializer& serializer, std::optional<std::size_t> len) noexcept :
            serializer(serializer) {
            if(len.has_value()) {
                entries.reserve(*len);
            }
        }

        template <typename K, typename V>
        status_t serialize_entry(const K& key, const V& value) {
            std::vector<std::function<void()>> writers;

            ETD_EXPECTED_TRY(serializer.collect_field(writers, detail::first_field, key));
            ETD_EXPECTED_TRY_V(auto value_field, detail::field_voffset(1));
            ETD_EXPECTED_TRY(serializer.collect_field(writers, value_field, value));
            ETD_EXPECTED_TRY_V(auto entry, serializer.finish_table(writers));
            entries.push_back(entry);

            return {};
        }

        result_t<value_type> end() {
            const auto offset = serializer.builder.CreateVector(entries);
            return serializer.wrap_offset(offset);
        }

    private:
        Serializer& serializer;
        std::vector<value_type> entries;
    };

    class SerializeStruct {
    public:
        explicit SerializeStruct(Serializer& serializer, std::size_t len) noexcept :
            serializer(serializer) {
            writers.reserve(len);
        }

        template <typename T>
        status_t serialize_field(std::string_view /*key*/, const T& value) {
            ETD_EXPECTED_TRY_V(auto field_id, detail::field_voffset(next_index));
            ++next_index;
            return serializer.collect_field(writers, field_id, value);
        }

        result_t<value_type> end() {
            return serializer.finish_table(writers);
        }

    private:
        Serializer& serializer;
        std::vector<std::function<void()>> writers;
        std::size_t next_index = 0;
    };

    explicit Serializer(std::size_t initial_capacity = 1024) : builder(initial_capacity) {}

    template <typename T>
    auto bytes(const T& value) -> result_t<std::vector<std::uint8_t>> {
        builder.Clear();

        ETD_EXPECTED_TRY_V(auto root, serde::serialize(*this, value));

        builder.Finish(root, detail::buffer_identifier);
        const auto* begin = builder.GetBufferPointer();
        return std::vector<std::uint8_t>(begin, begin + builder.GetSize());
    }

    result_t<value_type> serialize_null() {
        return encode_boxed(nullptr);
    }

    template <typename T>
    result_t<value_type> serialize_some(const T& value) {
        return serde::serialize(*this, value);
    }

    result_t<value_type> serialize_bool(bool value) {
        return encode_boxed(value);
    }

    result_t<value_type> serialize_int(std::int64_t value) {
        return encode_boxed(value);
    }

    result_t<value_type> serialize_uint(std::uint64_t value) {
        return encode_boxed(value);
    }

    result_t<value_type> serialize_float(double value) {
        return encode_boxed(value);
    }

    result_t<value_type> serialize_char(char value) {
        return encode_boxed(value);
    }

    result_t<value_type> serialize_str(std::string_view value) {
        return encode_boxed(value);
    }

    result_t<value_type> serialize_bytes(std::span<const std::byte> value) {
        return encode_boxed(value);
    }

    template <typename... Ts>
    result_t<value_type> serialize_variant(const std::variant<Ts...>& value) {
        return encode_variant(value);
    }

    result_t<SerializeSeq> serialize_seq(std::optional<std::size_t> len) {
        return SerializeSeq(*this, len);
    }

    result_t<SerializeTuple> serialize_tuple(std::size_t len) {
        return SerializeTuple(*this, len);
    }

    result_t<SerializeMap> serialize_map(std::optional<std::size_t> len) {
        return SerializeMap(*this, len);
    }

    result_t<SerializeStruct> serialize_struct(std::string_view /*name*/, std::size_t len) {
        return SerializeStruct(*this, len);
    }

    template <typename T>
    auto serialize_reflectable(const T& value) -> result_t<value_type> {
        using U = detail::remove_annotation_t<T>;
        if constexpr(can_inline_struct_v<U>) {
            return encode_boxed(value);
        } else {
            return encode_table(value);
        }
    }

    template <typename T>
    auto serialize_boxed(const T& value) -> result_t<value_type> {
        return encode_boxed(value);
    }

private:
    // Non-template helper to avoid lld issues with std::function vtable entries
    // for lambdas inside heavily-instantiated template member functions.
    void push_add_offset(std::vector<std::function<void()>>& writers,
                         ::flatbuffers::voffset_t field,
                         ::flatbuffers::uoffset_t raw_offset) {
        writers.push_back([this, field, raw_offset] {
            builder.AddOffset(field, ::flatbuffers::Offset<void>(raw_offset));
        });
    }

    template <typename OffsetT>
    auto wrap_offset(::flatbuffers::Offset<OffsetT> offset) -> result_t<value_type> {
        std::vector<std::function<void()>> writers;
        push_add_offset(writers, detail::first_field, offset.o);
        return finish_table(writers);
    }

    auto finish_table(std::vector<std::function<void()>>& writers) -> result_t<value_type> {
        const auto start = builder.StartTable();
        for(auto& write: writers) {
            write();
        }
        return value_type(builder.EndTable(start));
    }

    struct TableFieldCollector {
        Serializer<Config>* serializer = nullptr;
        std::vector<std::function<void()>>* writers = nullptr;
        std::size_t current_index = 0;

        template <typename V>
        auto serialize_field(std::string_view /*key*/, const V& field_value) -> status_t {
            if(serializer == nullptr || writers == nullptr) {
                return std::unexpected(object_error_code::invalid_state);
            }
            ETD_EXPECTED_TRY_V(auto field_id, detail::field_voffset(current_index));
            return serializer->collect_field(*writers, field_id, field_value);
        }
    };

    template <typename T>
    auto encode_boxed(const T& value) -> result_t<value_type> {
        std::vector<std::function<void()>> writers;
        ETD_EXPECTED_TRY(collect_field(writers, detail::first_field, value));
        return finish_table(writers);
    }

    template <typename T>
    auto encode_table(const T& value) -> result_t<value_type> {
        using U = detail::remove_annotation_t<T>;
        static_assert(refl::reflectable_class<U>, "encode_table requires reflectable class");

        std::vector<std::function<void()>> writers;
        TableFieldCollector collector{
            .serializer = this,
            .writers = &writers,
            .current_index = 0,
        };

        std::expected<void, object_error_code> field_result;
        refl::for_each(value, [&](auto field) {
            collector.current_index = field.index();
            auto status =
                serde::detail::serialize_struct_field<Config, object_error_code>(collector, field);
            if(!status) {
                field_result = std::unexpected(status.error());
                return false;
            }
            return true;
        });
        if(!field_result) {
            return std::unexpected(field_result.error());
        }

        return finish_table(writers);
    }

    template <typename T>
    auto encode_variant(const T& value) -> result_t<value_type> {
        using U = std::remove_cvref_t<T>;
        static_assert(is_specialization_of<std::variant, U>, "variant required");

        std::vector<std::function<void()>> writers;
        const auto variant_index = static_cast<std::uint32_t>(value.index());
        writers.push_back([this, variant_index] {
            builder.AddElement<std::uint32_t>(detail::first_field, variant_index);
        });

        std::expected<void, object_error_code> picked{};
        bool matched = false;
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (([&] {
                 if(value.index() != I) {
                     return;
                 }
                 matched = true;
                 auto field = detail::variant_field_voffset(I);
                 if(!field) {
                     picked = std::unexpected(field.error());
                     return;
                 }
                 picked = collect_field(writers, *field, std::get<I>(value));
             }()),
             ...);
        }(std::make_index_sequence<std::variant_size_v<U>>{});

        if(!matched) {
            return std::unexpected(object_error_code::invalid_state);
        }
        if(!picked) {
            return std::unexpected(picked.error());
        }

        return finish_table(writers);
    }

    template <typename T>
    auto encode_tuple_like(const T& value) -> result_t<value_type> {
        std::vector<std::function<void()>> writers;
        std::expected<void, object_error_code> status{};

        auto collect_one = [&](auto index_c, const auto& element) {
            constexpr std::size_t index = decltype(index_c)::value;
            auto field_id = detail::field_voffset(index);
            if(!field_id) {
                status = std::unexpected(field_id.error());
                return false;
            }
            auto collected = collect_field(writers, *field_id, element);
            if(!collected) {
                status = std::unexpected(collected.error());
                return false;
            }
            return true;
        };

        bool ok = [&]<std::size_t... I>(std::index_sequence<I...>) {
            return (collect_one(std::integral_constant<std::size_t, I>{}, std::get<I>(value)) &&
                    ...);
        }(std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<T>>>{});
        if(!ok) {
            return std::unexpected(status.error());
        }

        return finish_table(writers);
    }

    template <typename T>
    auto encode_map(const T& value) -> result_t<
        ::flatbuffers::Offset<::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::Table>>>> {
        using U = std::remove_cvref_t<T>;
        using key_t = typename U::key_type;
        using mapped_t = typename U::mapped_type;

        std::vector<std::pair<key_t, mapped_t>> entries;
        entries.reserve(value.size());
        for(const auto& [key, mapped]: value) {
            entries.emplace_back(key, mapped);
        }

        if constexpr(requires(const key_t& lhs, const key_t& rhs) {
                         { lhs < rhs } -> std::convertible_to<bool>;
                     }) {
            std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.first < rhs.first;
            });
        }

        std::vector<value_type> offsets;
        offsets.reserve(entries.size());
        for(const auto& [key, mapped]: entries) {
            std::vector<std::function<void()>> writers;
            ETD_EXPECTED_TRY(collect_field(writers, detail::first_field, key));
            ETD_EXPECTED_TRY_V(auto value_field, detail::field_voffset(1));
            ETD_EXPECTED_TRY(collect_field(writers, value_field, mapped));
            ETD_EXPECTED_TRY_V(auto entry, finish_table(writers));
            offsets.push_back(entry);
        }

        return builder.CreateVector(offsets);
    }

    template <typename T>
    auto collect_sequence_field(std::vector<std::function<void()>>& writers,
                                ::flatbuffers::voffset_t field,
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
            auto offset = builder.CreateVector(bytes);
            push_add_offset(writers, field, offset.o);
            return {};
        } else if constexpr(serde::bool_like<element_clean_t> || serde::int_like<element_clean_t> ||
                            serde::uint_like<element_clean_t>) {
            std::vector<element_clean_t> elements;
            if constexpr(requires { value.size(); }) {
                elements.reserve(value.size());
            }
            for(const auto& element: value) {
                elements.push_back(static_cast<element_clean_t>(element));
            }
            auto offset = builder.CreateVector(elements);
            push_add_offset(writers, field, offset.o);
            return {};
        } else if constexpr(serde::floating_like<element_clean_t>) {
            if constexpr(std::same_as<element_clean_t, float> ||
                         std::same_as<element_clean_t, double>) {
                std::vector<element_clean_t> elements;
                if constexpr(requires { value.size(); }) {
                    elements.reserve(value.size());
                }
                for(const auto& element: value) {
                    elements.push_back(static_cast<element_clean_t>(element));
                }
                auto offset = builder.CreateVector(elements);
                push_add_offset(writers, field, offset.o);
                return {};
            } else {
                std::vector<double> elements;
                if constexpr(requires { value.size(); }) {
                    elements.reserve(value.size());
                }
                for(const auto& element: value) {
                    elements.push_back(static_cast<double>(element));
                }
                auto offset = builder.CreateVector(elements);
                push_add_offset(writers, field, offset.o);
                return {};
            }
        } else if constexpr(serde::char_like<element_clean_t>) {
            std::vector<std::int8_t> elements;
            if constexpr(requires { value.size(); }) {
                elements.reserve(value.size());
            }
            for(const auto& element: value) {
                elements.push_back(static_cast<std::int8_t>(element));
            }
            auto offset = builder.CreateVector(elements);
            push_add_offset(writers, field, offset.o);
            return {};
        } else if constexpr(serde::str_like<element_clean_t>) {
            std::vector<::flatbuffers::Offset<::flatbuffers::String>> elements;
            if constexpr(requires { value.size(); }) {
                elements.reserve(value.size());
            }
            for(const auto& element: value) {
                const std::string_view text = element;
                elements.push_back(builder.CreateString(text.data(), text.size()));
            }
            auto offset = builder.CreateVector(elements);
            push_add_offset(writers, field, offset.o);
            return {};
        } else if constexpr(is_pair_v<element_clean_t> || is_tuple_v<element_clean_t>) {
            std::vector<value_type> elements;
            if constexpr(requires { value.size(); }) {
                elements.reserve(value.size());
            }
            for(const auto& element: value) {
                ETD_EXPECTED_TRY_V(auto tuple, encode_tuple_like(element));
                elements.push_back(tuple);
            }
            auto offset = builder.CreateVector(elements);
            push_add_offset(writers, field, offset.o);
            return {};
        } else if constexpr(can_inline_struct_v<element_clean_t>) {
            std::vector<element_clean_t> elements;
            if constexpr(requires { value.size(); }) {
                elements.reserve(value.size());
            }
            for(const auto& element: value) {
                elements.push_back(static_cast<element_clean_t>(element));
            }
            auto offset = builder.CreateVectorOfStructs(elements);
            push_add_offset(writers, field, offset.o);
            return {};
        } else if constexpr(refl::reflectable_class<element_clean_t>) {
            std::vector<value_type> elements;
            if constexpr(requires { value.size(); }) {
                elements.reserve(value.size());
            }
            for(const auto& element: value) {
                ETD_EXPECTED_TRY_V(auto table, encode_table(element));
                elements.push_back(table);
            }
            auto offset = builder.CreateVector(elements);
            push_add_offset(writers, field, offset.o);
            return {};
        } else {
            std::vector<value_type> elements;
            if constexpr(requires { value.size(); }) {
                elements.reserve(value.size());
            }
            for(const auto& element: value) {
                ETD_EXPECTED_TRY_V(auto boxed, encode_boxed(element));
                elements.push_back(boxed);
            }
            auto offset = builder.CreateVector(elements);
            push_add_offset(writers, field, offset.o);
            return {};
        }
    }

    template <typename T>
    auto collect_field(std::vector<std::function<void()>>& writers,
                       ::flatbuffers::voffset_t field,
                       const T& value) -> status_t {
        using U = std::remove_cvref_t<T>;
        using clean_t = detail::clean_t<U>;

        if constexpr(refl::annotated_type<U>) {
            return collect_field(writers, field, refl::annotated_value(value));
        } else if constexpr(is_specialization_of<std::optional, U>) {
            if(!value.has_value()) {
                return {};
            }
            return collect_field(writers, field, value.value());
        } else if constexpr(is_specialization_of<std::unique_ptr, U> ||
                            is_specialization_of<std::shared_ptr, U>) {
            if(!value) {
                return {};
            }
            return collect_field(writers, field, *value);
        } else if constexpr(std::same_as<clean_t, std::nullptr_t>) {
            return {};
        } else if constexpr(std::is_enum_v<clean_t>) {
            using underlying_t = std::underlying_type_t<clean_t>;
            return collect_field(writers, field, static_cast<underlying_t>(value));
        } else if constexpr(serde::bool_like<clean_t>) {
            const bool v = static_cast<bool>(value);
            writers.push_back([this, field, v] { builder.AddElement<bool>(field, v); });
            return {};
        } else if constexpr(serde::int_like<clean_t>) {
            const clean_t v = static_cast<clean_t>(value);
            writers.push_back([this, field, v] { builder.AddElement<clean_t>(field, v); });
            return {};
        } else if constexpr(serde::uint_like<clean_t>) {
            const clean_t v = static_cast<clean_t>(value);
            writers.push_back([this, field, v] { builder.AddElement<clean_t>(field, v); });
            return {};
        } else if constexpr(serde::floating_like<clean_t>) {
            if constexpr(std::same_as<clean_t, float> || std::same_as<clean_t, double>) {
                const clean_t v = static_cast<clean_t>(value);
                writers.push_back([this, field, v] { builder.AddElement<clean_t>(field, v); });
            } else {
                const double v = static_cast<double>(value);
                writers.push_back([this, field, v] { builder.AddElement<double>(field, v); });
            }
            return {};
        } else if constexpr(serde::char_like<clean_t>) {
            const std::int8_t v = static_cast<std::int8_t>(value);
            writers.push_back([this, field, v] { builder.AddElement<std::int8_t>(field, v); });
            return {};
        } else if constexpr(serde::str_like<clean_t>) {
            const std::string_view text = value;
            const auto offset = builder.CreateString(text.data(), text.size());
            push_add_offset(writers, field, offset.o);
            return {};
        } else if constexpr(serde::bytes_like<clean_t>) {
            const std::span<const std::byte> bytes = value;
            const auto* data =
                bytes.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(bytes.data());
            const auto offset = builder.CreateVector(data, bytes.size());
            push_add_offset(writers, field, offset.o);
            return {};
        } else if constexpr(is_specialization_of<std::variant, U>) {
            ETD_EXPECTED_TRY_V(auto offset, encode_variant(value));
            push_add_offset(writers, field, offset.o);
            return {};
        } else if constexpr(std::ranges::input_range<clean_t>) {
            constexpr auto kind = eventide::format_kind<clean_t>;
            if constexpr(kind == eventide::range_format::map) {
                ETD_EXPECTED_TRY_V(auto offset, encode_map(value));
                push_add_offset(writers, field, offset.o);
                return {};
            } else {
                return collect_sequence_field(writers, field, value);
            }
        } else if constexpr(is_pair_v<clean_t> || is_tuple_v<clean_t>) {
            ETD_EXPECTED_TRY_V(auto offset, encode_tuple_like(value));
            push_add_offset(writers, field, offset.o);
            return {};
        } else if constexpr(can_inline_struct_v<clean_t>) {
            const clean_t copy = static_cast<clean_t>(value);
            writers.push_back([this, field, copy] { builder.AddStruct(field, &copy); });
            return {};
        } else if constexpr(refl::reflectable_class<clean_t>) {
            ETD_EXPECTED_TRY_V(auto offset, encode_table(value));
            push_add_offset(writers, field, offset.o);
            return {};
        } else {
            return std::unexpected(object_error_code::unsupported_type);
        }
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

static_assert(serde::serializer_like<Serializer<>>);

}  // namespace eventide::serde::flatbuffers

namespace eventide::serde {

template <typename Config, typename T>
    requires refl::reflectable_class<std::remove_cvref_t<T>>
struct serialize_traits<flatbuffers::Serializer<Config>, T> {
    using serializer_t = flatbuffers::Serializer<Config>;

    static auto serialize(serializer_t& serializer, const T& value) ->
        typename serializer_t::template result_t<typename serializer_t::value_type> {
        return serializer.serialize_reflectable(value);
    }
};

template <typename Config, typename T>
    requires (!refl::reflectable_class<std::remove_cvref_t<T>> &&
              !std::ranges::input_range<std::remove_cvref_t<T>> &&
              !is_pair_v<std::remove_cvref_t<T>> && !is_tuple_v<std::remove_cvref_t<T>> &&
              !is_specialization_of<std::variant, std::remove_cvref_t<T>> &&
              (std::is_enum_v<flatbuffers::detail::clean_t<std::remove_cvref_t<T>>> ||
               serde::null_like<flatbuffers::detail::clean_t<std::remove_cvref_t<T>>> ||
               serde::bool_like<flatbuffers::detail::clean_t<std::remove_cvref_t<T>>> ||
               serde::int_like<flatbuffers::detail::clean_t<std::remove_cvref_t<T>>> ||
               serde::uint_like<flatbuffers::detail::clean_t<std::remove_cvref_t<T>>> ||
               serde::floating_like<flatbuffers::detail::clean_t<std::remove_cvref_t<T>>> ||
               serde::char_like<flatbuffers::detail::clean_t<std::remove_cvref_t<T>>> ||
               serde::str_like<flatbuffers::detail::clean_t<std::remove_cvref_t<T>>> ||
               serde::bytes_like<flatbuffers::detail::clean_t<std::remove_cvref_t<T>>>))
struct serialize_traits<flatbuffers::Serializer<Config>, T> {
    using serializer_t = flatbuffers::Serializer<Config>;

    static auto serialize(serializer_t& serializer, const T& value) ->
        typename serializer_t::template result_t<typename serializer_t::value_type> {
        return serializer.serialize_boxed(value);
    }
};

template <typename Config, typename T>
    requires (std::ranges::input_range<std::remove_cvref_t<T>> &&
              !refl::reflectable_class<std::remove_cvref_t<T>>)
struct serialize_traits<flatbuffers::Serializer<Config>, T> {
    using serializer_t = flatbuffers::Serializer<Config>;

    static auto serialize(serializer_t& serializer, const T& value) ->
        typename serializer_t::template result_t<typename serializer_t::value_type> {
        return serializer.serialize_boxed(value);
    }
};

}  // namespace eventide::serde
