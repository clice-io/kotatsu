#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
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
#include "eventide/serde/flatbuffers/serializer.h"
#include "eventide/serde/serde/config.h"
#include "eventide/serde/serde/utils/common.h"

#if __has_include(<flatbuffers/flatbuffers.h>)
#include <flatbuffers/flatbuffers.h>
#else
#error                                                                                             \
    "flatbuffers/flatbuffers.h not found. Enable ETD_SERDE_ENABLE_FLATBUFFERS or add flatbuffers include paths."
#endif

namespace eventide::serde::flatbuffers {

namespace deserialize_detail {

constexpr ::flatbuffers::voffset_t first_field = 4;
constexpr ::flatbuffers::voffset_t field_step = 2;

template <typename T>
using result_t = std::expected<T, object_error_code>;

using status_t = result_t<void>;

using serde::detail::clean_t;
using serde::detail::remove_annotation_t;
using serde::detail::remove_optional_t;

template <typename T>
constexpr bool root_unboxed_v =
    (refl::reflectable_class<T> && !can_inline_struct_v<T>) || is_pair_v<T> || is_tuple_v<T> ||
    is_specialization_of<std::variant, T>;

inline auto field_voffset(std::size_t index) -> ::flatbuffers::voffset_t {
    return static_cast<::flatbuffers::voffset_t>(static_cast<std::size_t>(first_field) +
                                                 index * static_cast<std::size_t>(field_step));
}

inline auto variant_field_voffset(std::size_t index) -> ::flatbuffers::voffset_t {
    return field_voffset(index + 1);
}

inline auto has_field(const ::flatbuffers::Table* table, ::flatbuffers::voffset_t field) -> bool {
    return table != nullptr && table->GetOptionalFieldOffset(field) != 0;
}

inline auto table_has_any_field(const ::flatbuffers::Table* table) -> bool {
    if(table == nullptr) {
        return false;
    }

    const auto* vtable = table->GetVTable();
    if(vtable == nullptr) {
        return false;
    }

    const auto vtable_size = ::flatbuffers::ReadScalar<::flatbuffers::voffset_t>(vtable);
    return vtable_size > static_cast<::flatbuffers::voffset_t>(4);
}

class Decoder {
public:
    explicit Decoder(const ::flatbuffers::Table* root) : root(root) {}

    template <typename T>
    auto decode(T& out) const -> status_t {
        return decode_root_value(out);
    }

private:
    template <typename T>
    auto decode_root_value(T& out) const -> status_t {
        using U = std::remove_cvref_t<T>;
        using clean_u_t = clean_t<U>;

        if constexpr(refl::annotated_type<U>) {
            return decode_root_value(refl::annotated_value(out));
        } else if constexpr(is_specialization_of<std::optional, U>) {
            using value_t = typename U::value_type;
            using clean_value_t = clean_t<value_t>;

            if constexpr(root_unboxed_v<clean_value_t>) {
                if(!table_has_any_field(root)) {
                    out.reset();
                    return {};
                }

                if(!out.has_value()) {
                    if constexpr(std::default_initializable<value_t>) {
                        out.emplace();
                    } else {
                        return std::unexpected(object_error_code::unsupported_type);
                    }
                }

                auto status = decode_unboxed(root, out.value());
                if(!status) {
                    out.reset();
                    return status;
                }
                return {};
            }

            if(has_field(root, first_field)) {
                if(!out.has_value()) {
                    if constexpr(std::default_initializable<value_t>) {
                        out.emplace();
                    } else {
                        return std::unexpected(object_error_code::unsupported_type);
                    }
                }

                auto status = decode_field(root, first_field, out.value(), true);
                if(!status) {
                    out.reset();
                    return status;
                }
                return {};
            }

            out.reset();
            return {};
        } else if constexpr(root_unboxed_v<clean_u_t>) {
            return decode_unboxed(root, out);
        } else {
            return decode_field(root, first_field, out, true);
        }
    }

    template <typename T>
    auto decode_unboxed(const ::flatbuffers::Table* table, T& out) const -> status_t {
        using U = clean_t<T>;
        if constexpr(refl::reflectable_class<U> && !can_inline_struct_v<U>) {
            return decode_table(table, out);
        } else if constexpr(is_pair_v<U> || is_tuple_v<U>) {
            return decode_tuple(table, out);
        } else if constexpr(is_specialization_of<std::variant, U>) {
            return decode_variant(table, out);
        } else {
            return std::unexpected(object_error_code::unsupported_type);
        }
    }

    template <typename T>
    auto decode_table(const ::flatbuffers::Table* table, T& out) const -> status_t {
        using U = std::remove_cvref_t<T>;
        static_assert(refl::reflectable_class<U>, "decode_table requires reflectable class");

        if(table == nullptr) {
            return std::unexpected(object_error_code::invalid_state);
        }

        std::expected<void, object_error_code> result{};
        refl::for_each(out, [&](auto field) {
            const auto field_id = field_voffset(field.index());
            auto status = decode_field(table, field_id, field.value(), false);
            if(!status) {
                result = std::unexpected(status.error());
                return false;
            }
            return true;
        });
        if(!result) {
            return std::unexpected(result.error());
        }
        return {};
    }

    template <typename T>
    auto decode_tuple(const ::flatbuffers::Table* table, T& out) const -> status_t {
        if(table == nullptr) {
            return std::unexpected(object_error_code::invalid_state);
        }

        std::expected<void, object_error_code> status{};
        auto decode_one = [&](auto index_c, auto& element) {
            constexpr std::size_t index = decltype(index_c)::value;
            const auto field = field_voffset(index);
            auto decoded = decode_field(table, field, element, false);
            if(!decoded) {
                status = std::unexpected(decoded.error());
                return false;
            }
            return true;
        };

        const bool ok = [&]<std::size_t... I>(std::index_sequence<I...>) {
            return (decode_one(std::integral_constant<std::size_t, I>{}, std::get<I>(out)) && ...);
        }(std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<T>>>{});

        if(!ok) {
            return std::unexpected(status.error());
        }
        return {};
    }

    template <typename T>
    auto decode_variant(const ::flatbuffers::Table* table, T& out) const -> status_t {
        using U = std::remove_cvref_t<T>;
        static_assert(is_specialization_of<std::variant, U>, "decode_variant requires variant");

        if(table == nullptr) {
            return std::unexpected(object_error_code::invalid_state);
        }
        if(!has_field(table, first_field)) {
            return std::unexpected(object_error_code::invalid_state);
        }

        const auto index =
            static_cast<std::size_t>(table->GetField<std::uint32_t>(first_field, 0U));
        if(index >= std::variant_size_v<U>) {
            return std::unexpected(object_error_code::invalid_state);
        }

        std::expected<void, object_error_code> status{};
        bool matched = false;
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (([&] {
                 if(index != I) {
                     return;
                 }
                 matched = true;
                 status = [&, value_field = variant_field_voffset(I)]() -> status_t {
                     using alt_t = std::variant_alternative_t<I, U>;
                     if constexpr(!std::default_initializable<alt_t>) {
                         return std::unexpected(object_error_code::unsupported_type);
                     } else {
                         alt_t alt{};
                         auto decoded = decode_field(table, value_field, alt, true);
                         if(!decoded) {
                             return std::unexpected(decoded.error());
                         }
                         out = std::move(alt);
                         return {};
                     }
                 }();
             }()),
             ...);
        }(std::make_index_sequence<std::variant_size_v<U>>{});

        if(!matched) {
            return std::unexpected(object_error_code::invalid_state);
        }
        if(!status) {
            return std::unexpected(status.error());
        }
        return {};
    }

    template <typename T>
    auto decode_sequence(const ::flatbuffers::Table* table,
                         ::flatbuffers::voffset_t field,
                         T& out,
                         bool required) const -> status_t {
        using U = std::remove_cvref_t<T>;
        using element_t = std::ranges::range_value_t<U>;
        using element_clean_t = clean_t<element_t>;

        if(!has_field(table, field)) {
            if(required) {
                return std::unexpected(object_error_code::invalid_state);
            }
            return {};
        }

        if constexpr(requires { out.clear(); }) {
            out.clear();
        }

        constexpr bool index_assignable_fixed_size =
            !eventide::detail::sequence_insertable<U, element_t> &&
            requires(U& container, std::size_t index, element_t value) {
                std::tuple_size<U>::value;
                container[index] = std::move(value);
            };

        std::size_t written_count = 0;
        auto store_element = [&](auto&& element) -> status_t {
            if constexpr(index_assignable_fixed_size) {
                constexpr auto expected_count = std::tuple_size_v<U>;
                if(written_count >= expected_count) {
                    return std::unexpected(object_error_code::invalid_state);
                }
                out[written_count] =
                    static_cast<element_t>(std::forward<decltype(element)>(element));
                ++written_count;
                return {};
            } else {
                auto ok = eventide::detail::append_sequence_element(
                    out,
                    static_cast<element_t>(std::forward<decltype(element)>(element)));
                if(!ok) {
                    return std::unexpected(object_error_code::unsupported_type);
                }
                return {};
            }
        };

        auto finalize_sequence = [&]() -> status_t {
            if constexpr(index_assignable_fixed_size) {
                constexpr auto expected_count = std::tuple_size_v<U>;
                if(written_count != expected_count) {
                    return std::unexpected(object_error_code::invalid_state);
                }
            }
            return {};
        };

        if constexpr(std::same_as<element_clean_t, std::byte>) {
            const auto* vector =
                table->GetPointer<const ::flatbuffers::Vector<std::uint8_t>*>(field);
            if(vector == nullptr) {
                return std::unexpected(object_error_code::invalid_state);
            }
            for(std::size_t i = 0; i < vector->size(); ++i) {
                ETD_EXPECTED_TRY(store_element(
                    std::byte{vector->Get(static_cast<::flatbuffers::uoffset_t>(i))}));
            }
            return finalize_sequence();
        } else if constexpr(serde::bool_like<element_clean_t> || serde::int_like<element_clean_t> ||
                            serde::uint_like<element_clean_t>) {
            const auto* vector =
                table->GetPointer<const ::flatbuffers::Vector<element_clean_t>*>(field);
            if(vector == nullptr) {
                return std::unexpected(object_error_code::invalid_state);
            }
            for(std::size_t i = 0; i < vector->size(); ++i) {
                ETD_EXPECTED_TRY(
                    store_element(vector->Get(static_cast<::flatbuffers::uoffset_t>(i))));
            }
            return finalize_sequence();
        } else if constexpr(serde::floating_like<element_clean_t>) {
            if constexpr(std::same_as<element_clean_t, float> ||
                         std::same_as<element_clean_t, double>) {
                const auto* vector =
                    table->GetPointer<const ::flatbuffers::Vector<element_clean_t>*>(field);
                if(vector == nullptr) {
                    return std::unexpected(object_error_code::invalid_state);
                }
                for(std::size_t i = 0; i < vector->size(); ++i) {
                    ETD_EXPECTED_TRY(
                        store_element(vector->Get(static_cast<::flatbuffers::uoffset_t>(i))));
                }
                return finalize_sequence();
            } else {
                const auto* vector = table->GetPointer<const ::flatbuffers::Vector<double>*>(field);
                if(vector == nullptr) {
                    return std::unexpected(object_error_code::invalid_state);
                }
                for(std::size_t i = 0; i < vector->size(); ++i) {
                    ETD_EXPECTED_TRY(
                        store_element(vector->Get(static_cast<::flatbuffers::uoffset_t>(i))));
                }
                return finalize_sequence();
            }
        } else if constexpr(serde::char_like<element_clean_t>) {
            const auto* vector =
                table->GetPointer<const ::flatbuffers::Vector<std::int8_t>*>(field);
            if(vector == nullptr) {
                return std::unexpected(object_error_code::invalid_state);
            }
            for(std::size_t i = 0; i < vector->size(); ++i) {
                ETD_EXPECTED_TRY(store_element(
                    static_cast<char>(vector->Get(static_cast<::flatbuffers::uoffset_t>(i)))));
            }
            return finalize_sequence();
        } else if constexpr(std::is_enum_v<element_clean_t>) {
            using storage_t = std::underlying_type_t<element_clean_t>;
            const auto* vector = table->GetPointer<const ::flatbuffers::Vector<storage_t>*>(field);
            if(vector == nullptr) {
                return std::unexpected(object_error_code::invalid_state);
            }
            for(std::size_t i = 0; i < vector->size(); ++i) {
                ETD_EXPECTED_TRY(store_element(static_cast<element_clean_t>(
                    vector->Get(static_cast<::flatbuffers::uoffset_t>(i)))));
            }
            return finalize_sequence();
        } else if constexpr(serde::str_like<element_clean_t>) {
            const auto* vector = table->GetPointer<
                const ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>>*>(field);
            if(vector == nullptr) {
                return std::unexpected(object_error_code::invalid_state);
            }
            for(std::size_t i = 0; i < vector->size(); ++i) {
                const auto* text = vector->GetAsString(static_cast<::flatbuffers::uoffset_t>(i));
                if(text == nullptr) {
                    return std::unexpected(object_error_code::invalid_state);
                }
                if constexpr(std::same_as<element_t, std::string> ||
                             std::derived_from<element_t, std::string>) {
                    ETD_EXPECTED_TRY(store_element(std::string(text->data(), text->size())));
                } else if constexpr(std::same_as<element_t, std::string_view>) {
                    ETD_EXPECTED_TRY(store_element(std::string_view(text->data(), text->size())));
                } else {
                    return std::unexpected(object_error_code::unsupported_type);
                }
            }
            return finalize_sequence();
        } else if constexpr(is_pair_v<element_clean_t> || is_tuple_v<element_clean_t>) {
            const auto* vector = table->GetPointer<
                const ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::Table>>*>(field);
            if(vector == nullptr) {
                return std::unexpected(object_error_code::invalid_state);
            }
            for(std::size_t i = 0; i < vector->size(); ++i) {
                using dec_t = std::remove_cvref_t<element_t>;
                if constexpr(!std::default_initializable<dec_t>) {
                    return std::unexpected(object_error_code::unsupported_type);
                } else {
                    dec_t element{};
                    const auto* nested = vector->GetAs<::flatbuffers::Table>(
                        static_cast<::flatbuffers::uoffset_t>(i));
                    ETD_EXPECTED_TRY(decode_tuple(nested, element));
                    ETD_EXPECTED_TRY(store_element(std::move(element)));
                }
            }
            return finalize_sequence();
        } else if constexpr(can_inline_struct_v<element_clean_t>) {
            const auto* vector =
                table->GetPointer<const ::flatbuffers::Vector<const element_clean_t*>*>(field);
            if(vector == nullptr) {
                return std::unexpected(object_error_code::invalid_state);
            }
            for(std::size_t i = 0; i < vector->size(); ++i) {
                const auto* element = vector->Get(static_cast<::flatbuffers::uoffset_t>(i));
                if(element == nullptr) {
                    return std::unexpected(object_error_code::invalid_state);
                }
                ETD_EXPECTED_TRY(store_element(*element));
            }
            return finalize_sequence();
        } else if constexpr(refl::reflectable_class<element_clean_t>) {
            const auto* vector = table->GetPointer<
                const ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::Table>>*>(field);
            if(vector == nullptr) {
                return std::unexpected(object_error_code::invalid_state);
            }
            for(std::size_t i = 0; i < vector->size(); ++i) {
                using dec_t = std::remove_cvref_t<element_t>;
                if constexpr(!std::default_initializable<dec_t>) {
                    return std::unexpected(object_error_code::unsupported_type);
                } else {
                    dec_t element{};
                    const auto* nested = vector->GetAs<::flatbuffers::Table>(
                        static_cast<::flatbuffers::uoffset_t>(i));
                    ETD_EXPECTED_TRY(decode_table(nested, element));
                    ETD_EXPECTED_TRY(store_element(std::move(element)));
                }
            }
            return finalize_sequence();
        } else {
            const auto* vector = table->GetPointer<
                const ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::Table>>*>(field);
            if(vector == nullptr) {
                return std::unexpected(object_error_code::invalid_state);
            }
            for(std::size_t i = 0; i < vector->size(); ++i) {
                using dec_t = std::remove_cvref_t<element_t>;
                if constexpr(!std::default_initializable<dec_t>) {
                    return std::unexpected(object_error_code::unsupported_type);
                } else {
                    dec_t element{};
                    const auto* nested = vector->GetAs<::flatbuffers::Table>(
                        static_cast<::flatbuffers::uoffset_t>(i));
                    ETD_EXPECTED_TRY(decode_root_value_from_table(nested, element));
                    ETD_EXPECTED_TRY(store_element(std::move(element)));
                }
            }
            return finalize_sequence();
        }
    }

    template <typename T>
    auto decode_map(const ::flatbuffers::Table* table,
                    ::flatbuffers::voffset_t field,
                    T& out,
                    bool required) const -> status_t {
        using U = std::remove_cvref_t<T>;
        using key_t = typename U::key_type;
        using mapped_t = typename U::mapped_type;

        if(!has_field(table, field)) {
            if(required) {
                return std::unexpected(object_error_code::invalid_state);
            }
            return {};
        }

        if constexpr(requires { out.clear(); }) {
            out.clear();
        }
        static_assert(eventide::detail::map_insertable<U, key_t, mapped_t>,
                      "from_flatbuffer map requires insertable container");

        const auto* entries = table->GetPointer<
            const ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::Table>>*>(field);
        if(entries == nullptr) {
            return std::unexpected(object_error_code::invalid_state);
        }

        for(std::size_t i = 0; i < entries->size(); ++i) {
            auto* entry =
                entries->GetAs<::flatbuffers::Table>(static_cast<::flatbuffers::uoffset_t>(i));
            if(entry == nullptr) {
                return std::unexpected(object_error_code::invalid_state);
            }

            key_t key{};
            mapped_t mapped{};
            ETD_EXPECTED_TRY(decode_field(entry, first_field, key, true));
            ETD_EXPECTED_TRY(decode_field(entry, field_voffset(1), mapped, true));

            auto ok = eventide::detail::insert_map_entry(out, std::move(key), std::move(mapped));
            if(!ok) {
                return std::unexpected(object_error_code::unsupported_type);
            }
        }

        return {};
    }

    template <typename T>
    auto decode_field(const ::flatbuffers::Table* table,
                      ::flatbuffers::voffset_t field,
                      T& out,
                      bool required) const -> status_t {
        using U = std::remove_cvref_t<T>;
        using clean_u_t = clean_t<U>;

        if constexpr(refl::annotated_type<U>) {
            return decode_field(table, field, refl::annotated_value(out), required);
        } else if constexpr(is_specialization_of<std::optional, U>) {
            using value_t = typename U::value_type;
            if(!has_field(table, field)) {
                out.reset();
                return {};
            }

            if(!out.has_value()) {
                if constexpr(std::default_initializable<value_t>) {
                    out.emplace();
                } else {
                    return std::unexpected(object_error_code::unsupported_type);
                }
            }

            auto status = decode_field(table, field, out.value(), true);
            if(!status) {
                out.reset();
                return status;
            }
            return {};
        } else if constexpr(is_specialization_of<std::unique_ptr, U>) {
            using value_t = typename U::element_type;
            static_assert(std::default_initializable<value_t>,
                          "from_flatbuffer unique_ptr requires default-constructible pointee");
            static_assert(std::same_as<typename U::deleter_type, std::default_delete<value_t>>,
                          "from_flatbuffer unique_ptr with custom deleter is unsupported");

            if(!has_field(table, field)) {
                out.reset();
                return {};
            }

            auto value = std::make_unique<value_t>();
            auto status = decode_field(table, field, *value, true);
            if(!status) {
                return status;
            }
            out = std::move(value);
            return {};
        } else if constexpr(is_specialization_of<std::shared_ptr, U>) {
            using value_t = typename U::element_type;
            static_assert(std::default_initializable<value_t>,
                          "from_flatbuffer shared_ptr requires default-constructible pointee");

            if(!has_field(table, field)) {
                out.reset();
                return {};
            }

            auto value = std::make_shared<value_t>();
            auto status = decode_field(table, field, *value, true);
            if(!status) {
                return status;
            }
            out = std::move(value);
            return {};
        } else {
            if(!has_field(table, field)) {
                if(required) {
                    return std::unexpected(object_error_code::invalid_state);
                }
                return {};
            }

            if constexpr(std::same_as<clean_u_t, std::nullptr_t>) {
                return {};
            } else if constexpr(std::is_enum_v<clean_u_t>) {
                using underlying_t = std::underlying_type_t<clean_u_t>;
                auto value = table->GetField<underlying_t>(field, underlying_t{});
                out = static_cast<U>(static_cast<clean_u_t>(value));
                return {};
            } else if constexpr(serde::bool_like<clean_u_t> || serde::int_like<clean_u_t> ||
                                serde::uint_like<clean_u_t>) {
                out = static_cast<U>(table->GetField<clean_u_t>(field, clean_u_t{}));
                return {};
            } else if constexpr(serde::floating_like<clean_u_t>) {
                if constexpr(std::same_as<clean_u_t, float> || std::same_as<clean_u_t, double>) {
                    out = static_cast<U>(table->GetField<clean_u_t>(field, clean_u_t{}));
                } else {
                    out = static_cast<U>(table->GetField<double>(field, 0.0));
                }
                return {};
            } else if constexpr(serde::char_like<clean_u_t>) {
                out = static_cast<U>(static_cast<char>(table->GetField<std::int8_t>(field, 0)));
                return {};
            } else if constexpr(serde::str_like<clean_u_t>) {
                const auto* text = table->GetPointer<const ::flatbuffers::String*>(field);
                if(text == nullptr) {
                    return std::unexpected(object_error_code::invalid_state);
                }

                if constexpr(std::same_as<U, std::string> || std::derived_from<U, std::string>) {
                    out.assign(text->data(), text->size());
                    return {};
                } else if constexpr(std::same_as<U, std::string_view>) {
                    out = std::string_view(text->data(), text->size());
                    return {};
                } else {
                    return std::unexpected(object_error_code::unsupported_type);
                }
            } else if constexpr(serde::bytes_like<clean_u_t>) {
                if constexpr(std::same_as<U, std::vector<std::byte>>) {
                    const auto* bytes =
                        table->GetPointer<const ::flatbuffers::Vector<std::uint8_t>*>(field);
                    if(bytes == nullptr) {
                        return std::unexpected(object_error_code::invalid_state);
                    }

                    out.resize(bytes->size());
                    for(std::size_t i = 0; i < bytes->size(); ++i) {
                        out[i] = std::byte{bytes->Get(static_cast<::flatbuffers::uoffset_t>(i))};
                    }
                    return {};
                } else {
                    return std::unexpected(object_error_code::unsupported_type);
                }
            } else if constexpr(is_specialization_of<std::variant, U>) {
                const auto* nested = table->GetPointer<const ::flatbuffers::Table*>(field);
                return decode_variant(nested, out);
            } else if constexpr(std::ranges::input_range<clean_u_t>) {
                constexpr auto kind = eventide::format_kind<clean_u_t>;
                if constexpr(kind == eventide::range_format::map) {
                    return decode_map(table, field, out, required);
                } else {
                    return decode_sequence(table, field, out, required);
                }
            } else if constexpr(is_pair_v<clean_u_t> || is_tuple_v<clean_u_t>) {
                const auto* nested = table->GetPointer<const ::flatbuffers::Table*>(field);
                return decode_tuple(nested, out);
            } else if constexpr(can_inline_struct_v<clean_u_t>) {
                const auto* value = table->GetStruct<const clean_u_t*>(field);
                if(value == nullptr) {
                    return std::unexpected(object_error_code::invalid_state);
                }
                out = static_cast<U>(*value);
                return {};
            } else if constexpr(refl::reflectable_class<clean_u_t>) {
                const auto* nested = table->GetPointer<const ::flatbuffers::Table*>(field);
                return decode_table(nested, out);
            } else {
                return std::unexpected(object_error_code::unsupported_type);
            }
        }
    }

    template <typename T>
    auto decode_root_value_from_table(const ::flatbuffers::Table* table, T& out) const -> status_t {
        using U = std::remove_cvref_t<T>;
        using clean_u_t = clean_t<U>;

        if constexpr(refl::annotated_type<U>) {
            return decode_root_value_from_table(table, refl::annotated_value(out));
        } else if constexpr(root_unboxed_v<clean_u_t>) {
            return decode_unboxed(table, out);
        } else {
            return decode_field(table, first_field, out, true);
        }
    }

private:
    const ::flatbuffers::Table* root = nullptr;
};

}  // namespace deserialize_detail

template <typename Config = config::default_config>
class Deserializer {
public:
    using config_type = Config;
    using error_type = object_error_code;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    explicit Deserializer(std::span<const std::uint8_t> bytes) {
        initialize(bytes);
    }

    explicit Deserializer(std::span<const std::byte> bytes) {
        if(bytes.empty()) {
            initialize(std::span<const std::uint8_t>{});
            return;
        }
        const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
        initialize(std::span<const std::uint8_t>(data, bytes.size()));
    }

    explicit Deserializer(const std::vector<std::uint8_t>& bytes) :
        Deserializer(std::span<const std::uint8_t>(bytes.data(), bytes.size())) {}

    auto valid() const -> bool {
        return is_valid;
    }

    auto error() const -> error_type {
        if(is_valid) {
            return object_error_code::none;
        }
        return last_error;
    }

    template <typename T>
    auto deserialize(T& value) const -> result_t<void> {
        if(!is_valid || root == nullptr) {
            return std::unexpected(last_error);
        }
        return deserialize_detail::Decoder(root).decode(value);
    }

private:
    auto initialize(std::span<const std::uint8_t> bytes) -> void {
        if(bytes.empty()) {
            set_invalid(object_error_code::invalid_state);
            return;
        }
        if(!::flatbuffers::BufferHasIdentifier(
               bytes.data(),
               ::eventide::serde::flatbuffers::detail::buffer_identifier)) {
            set_invalid(object_error_code::invalid_state);
            return;
        }

        auto* table = ::flatbuffers::GetRoot<::flatbuffers::Table>(bytes.data());
        if(table == nullptr) {
            set_invalid(object_error_code::invalid_state);
            return;
        }

        ::flatbuffers::Verifier verifier(bytes.data(), bytes.size());
        if(!table->VerifyTableStart(verifier) || !verifier.EndTable()) {
            set_invalid(object_error_code::invalid_state);
            return;
        }

        root = table;
    }

    auto set_invalid(error_type error) -> void {
        is_valid = false;
        last_error = error;
        root = nullptr;
    }

private:
    bool is_valid = true;
    error_type last_error = object_error_code::none;
    const ::flatbuffers::Table* root = nullptr;
};

template <typename Config = config::default_config, typename T>
auto from_flatbuffer(std::span<const std::uint8_t> bytes, T& value)
    -> std::expected<void, object_error_code> {
    Deserializer<Config> deserializer(bytes);
    if(!deserializer.valid()) {
        return std::unexpected(deserializer.error());
    }

    ETD_EXPECTED_TRY(deserializer.deserialize(value));
    return {};
}

template <typename Config = config::default_config, typename T>
auto from_flatbuffer(std::span<const std::byte> bytes, T& value)
    -> std::expected<void, object_error_code> {
    Deserializer<Config> deserializer(bytes);
    if(!deserializer.valid()) {
        return std::unexpected(deserializer.error());
    }

    ETD_EXPECTED_TRY(deserializer.deserialize(value));
    return {};
}

template <typename Config = config::default_config, typename T>
auto from_flatbuffer(const std::vector<std::uint8_t>& bytes, T& value)
    -> std::expected<void, object_error_code> {
    return from_flatbuffer<Config>(std::span<const std::uint8_t>(bytes.data(), bytes.size()),
                                   value);
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_flatbuffer(std::span<const std::uint8_t> bytes) -> std::expected<T, object_error_code> {
    T value{};
    ETD_EXPECTED_TRY(from_flatbuffer<Config>(bytes, value));
    return value;
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_flatbuffer(std::span<const std::byte> bytes) -> std::expected<T, object_error_code> {
    T value{};
    ETD_EXPECTED_TRY(from_flatbuffer<Config>(bytes, value));
    return value;
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_flatbuffer(const std::vector<std::uint8_t>& bytes)
    -> std::expected<T, object_error_code> {
    return from_flatbuffer<T, Config>(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
}

}  // namespace eventide::serde::flatbuffers
