#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "eventide/serde/serde/serde.h"
#include "eventide/serde/serde/utils/common.h"

namespace eventide::serde::flatbuffers {

namespace schema_detail {

using serde::detail::remove_annotation_t;
using serde::detail::remove_optional_t;
using serde::detail::clean_t;

template <typename T>
constexpr bool is_std_vector_v = is_specialization_of<std::vector, std::remove_cvref_t<T>>;

template <typename T>
constexpr bool is_std_map_v = is_specialization_of<std::map, std::remove_cvref_t<T>>;

template <typename T>
constexpr bool is_scalar_field_v =
    std::same_as<T, bool> || serde::int_like<T> || serde::uint_like<T> || serde::floating_like<T>;

inline std::string normalize_identifier(std::string_view text) {
    std::string out;
    out.reserve(text.size());

    for(char c: text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if(std::isalnum(u)) {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }

    if(out.empty()) {
        out = "Unnamed";
    }

    if(std::isdigit(static_cast<unsigned char>(out.front())) != 0) {
        out.insert(out.begin(), '_');
    }
    return out;
}

template <typename T>
std::string type_identifier() {
    return normalize_identifier(refl::type_name<T>());
}

template <typename T>
std::string scalar_schema_name() {
    using U = remove_optional_t<T>;

    if constexpr(std::same_as<U, bool>) {
        return "bool";
    } else if constexpr(std::same_as<U, std::int8_t> || std::same_as<U, signed char> ||
                        std::same_as<U, char>) {
        return "byte";
    } else if constexpr(std::same_as<U, std::uint8_t> || std::same_as<U, unsigned char>) {
        return "ubyte";
    } else if constexpr(std::same_as<U, std::int16_t> || std::same_as<U, short>) {
        return "short";
    } else if constexpr(std::same_as<U, std::uint16_t> || std::same_as<U, unsigned short>) {
        return "ushort";
    } else if constexpr(std::same_as<U, std::int32_t> || std::same_as<U, int>) {
        return "int";
    } else if constexpr(std::same_as<U, std::uint32_t> || std::same_as<U, unsigned int>) {
        return "uint";
    } else if constexpr(std::same_as<U, std::int64_t> || std::same_as<U, long long>) {
        return "long";
    } else if constexpr(std::same_as<U, std::uint64_t> || std::same_as<U, unsigned long long>) {
        return "ulong";
    } else if constexpr(std::same_as<U, float>) {
        return "float";
    } else if constexpr(std::same_as<U, double>) {
        return "double";
    } else if constexpr(std::is_enum_v<U>) {
        using underlying_t = std::underlying_type_t<U>;
        return scalar_schema_name<underlying_t>();
    } else {
        static_assert(dependent_false<U>, "unsupported scalar schema type");
    }
}

template <typename T>
struct schema_struct_trait;

template <typename T>
constexpr bool is_schema_struct_field_v = [] {
    using U = remove_optional_t<T>;
    if constexpr(is_scalar_field_v<U> || std::is_enum_v<U>) {
        return true;
    } else if constexpr(refl::reflectable_class<U>) {
        return schema_struct_trait<U>::value;
    } else {
        return false;
    }
}();

template <typename T>
struct schema_struct_trait {
    static consteval bool fields_supported() {
        if constexpr(!refl::reflectable_class<T>) {
            return false;
        } else {
            return []<std::size_t... I>(std::index_sequence<I...>) {
                return (is_schema_struct_field_v<refl::field_type<T, I>> && ...);
            }(std::make_index_sequence<refl::field_count<T>()>{});
        }
    }

    constexpr static bool value = refl::reflectable_class<T> && std::is_trivial_v<T> &&
                                  std::is_standard_layout_v<T> && fields_supported();
};

template <typename T>
constexpr bool is_schema_struct_v = schema_struct_trait<T>::value;

template <typename T>
constexpr bool is_string_like_field_v = std::same_as<remove_optional_t<T>, std::string> ||
                                        std::same_as<remove_optional_t<T>, std::string_view>;

inline std::string map_entry_identifier(std::string_view owner_name, std::string_view field_name) {
    return normalize_identifier(std::string(owner_name) + "_" + std::string(field_name) + "Entry");
}

class schema_emitter {
public:
    template <typename Root>
    std::string emit() {
        emit_dependencies<Root>();
        emit_object_if_needed<Root>();
        out += "root_type " + type_identifier<Root>() + ";\n";
        return out;
    }

private:
    template <typename T>
    void emit_dependencies() {
        using U = remove_optional_t<T>;
        if constexpr(std::is_enum_v<U>) {
            emit_enum_if_needed<U>();
        } else if constexpr(is_std_vector_v<U>) {
            using element_t = typename U::value_type;
            emit_dependencies<element_t>();
        } else if constexpr(is_std_map_v<U>) {
            using key_t = typename U::key_type;
            using mapped_t = typename U::mapped_type;
            emit_dependencies<key_t>();
            emit_dependencies<mapped_t>();
        } else if constexpr(refl::reflectable_class<U>) {
            emit_object_if_needed<U>();
        }
    }

    template <typename E>
    void emit_enum_if_needed() {
        static_assert(std::is_enum_v<E>, "enum required");
        const auto enum_name = type_identifier<E>();
        if(!emitted_enums.insert(enum_name).second) {
            return;
        }

        out += "enum " + enum_name + ":" + scalar_schema_name<std::underlying_type_t<E>>() + " {\n";

        const auto& names = refl::reflection<E>::member_names;
        const auto& values = refl::reflection<E>::member_values;
        for(std::size_t i = 0; i < names.size(); ++i) {
            const auto member_name = normalize_identifier(names[i]);
            const auto member_value =
                static_cast<long long>(static_cast<std::underlying_type_t<E>>(values[i]));
            out += "  " + member_name + " = " + std::to_string(member_value);
            out += (i + 1 < names.size()) ? ",\n" : "\n";
        }
        out += "}\n\n";
    }

    template <typename Owner, std::size_t I>
    void emit_map_entry_if_needed() {
        using field_t = remove_optional_t<refl::field_type<Owner, I>>;
        if constexpr(!is_std_map_v<field_t>) {
            return;
        } else {
            constexpr auto field_name = refl::field_name<I, Owner>();
            const auto owner_name = type_identifier<Owner>();
            const auto entry_name = map_entry_identifier(owner_name, field_name);
            if(!emitted_entries.insert(entry_name).second) {
                return;
            }

            using key_t = typename field_t::key_type;
            using mapped_t = typename field_t::mapped_type;
            emit_dependencies<key_t>();
            emit_dependencies<mapped_t>();

            out += "table " + entry_name + " {\n";
            out += "  key:" + field_schema_type<key_t, Owner, I>() + " (key);\n";
            out += "  value:" + field_schema_type<mapped_t, Owner, I>() + ";\n";
            out += "}\n\n";
        }
    }

    template <typename T, typename Owner, std::size_t FieldIndex>
    std::string field_schema_type() {
        using U = remove_optional_t<T>;
        if constexpr(is_scalar_field_v<U> || std::is_enum_v<U>) {
            return std::is_enum_v<U> ? type_identifier<U>() : scalar_schema_name<U>();
        } else if constexpr(is_string_like_field_v<U>) {
            return "string";
        } else if constexpr(is_std_vector_v<U>) {
            using element_t = typename U::value_type;
            return "[" + field_schema_type<element_t, Owner, FieldIndex>() + "]";
        } else if constexpr(is_std_map_v<U>) {
            const auto owner_name = type_identifier<Owner>();
            constexpr auto field_name = refl::field_name<FieldIndex, Owner>();
            return "[" + map_entry_identifier(owner_name, field_name) + "]";
        } else if constexpr(refl::reflectable_class<U>) {
            return type_identifier<U>();
        } else {
            static_assert(dependent_false<U>, "unsupported field type for schema emission");
        }
    }

    template <typename T>
    void emit_object_if_needed() {
        static_assert(refl::reflectable_class<T>, "reflectable type required");

        const auto object_name = type_identifier<T>();
        if(!emitted_objects.insert(object_name).second) {
            return;
        }

        []<std::size_t... I>(schema_emitter* self, std::index_sequence<I...>) {
            (self->template emit_dependencies<refl::field_type<T, I>>(), ...);
        }(this, std::make_index_sequence<refl::field_count<T>()>{});

        []<std::size_t... I>(schema_emitter* self, std::index_sequence<I...>) {
            (self->template emit_map_entry_if_needed<T, I>(), ...);
        }(this, std::make_index_sequence<refl::field_count<T>()>{});

        out += (is_schema_struct_v<T> ? "struct " : "table ");
        out += object_name + " {\n";

        []<std::size_t... I>(schema_emitter* self, std::index_sequence<I...>) {
            ((self->out += "  " + normalize_identifier(refl::field_name<I, T>()) + ":" +
                           self->template field_schema_type<refl::field_type<T, I>, T, I>() +
                           ";\n"),
             ...);
        }(this, std::make_index_sequence<refl::field_count<T>()>{});

        out += "}\n\n";
    }

private:
    std::string out;
    std::set<std::string> emitted_objects;
    std::set<std::string> emitted_enums;
    std::set<std::string> emitted_entries;
};

}  // namespace schema_detail

template <typename T>
constexpr bool is_schema_struct_v = schema_detail::is_schema_struct_v<T>;

template <typename T>
consteval bool has_annotated_fields() {
    using U = std::remove_cvref_t<T>;
    if constexpr(!refl::reflectable_class<U>) {
        return false;
    } else {
        return []<std::size_t... I>(std::index_sequence<I...>) {
            return (refl::annotated_type<refl::field_type<U, I>> || ...);
        }(std::make_index_sequence<refl::field_count<U>()>{});
    }
}

template <typename T>
constexpr bool can_inline_struct_v =
    refl::reflectable_class<T> && is_schema_struct_v<T> && !has_annotated_fields<T>();

template <typename T>
std::string type_identifier() {
    return schema_detail::type_identifier<T>();
}

template <typename Root>
std::string render() {
    static_assert(refl::reflectable_class<Root>, "render requires a reflectable root type");
    return schema_detail::schema_emitter{}.template emit<Root>();
}

template <typename Key, typename Value>
struct map_entry {
    Key key{};
    Value value{};
};

template <typename Map>
auto to_sorted_entries(const Map& map)
    -> std::vector<map_entry<typename Map::key_type, typename Map::mapped_type>> {
    using key_t = typename Map::key_type;
    using mapped_t = typename Map::mapped_type;

    std::vector<map_entry<key_t, mapped_t>> entries;
    entries.reserve(map.size());
    for(const auto& [key, value]: map) {
        entries.push_back(map_entry<key_t, mapped_t>{key, value});
    }

    std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.key < rhs.key;
    });
    return entries;
}

template <typename EntryVec, typename Key>
auto bsearch_entry(const EntryVec& entries, const Key& key) -> const
    typename EntryVec::value_type* {
    auto it =
        std::lower_bound(entries.begin(), entries.end(), key, [](const auto& entry, const auto& k) {
            return entry.key < k;
        });
    if(it == entries.end() || it->key != key) {
        return nullptr;
    }
    return &(*it);
}

}  // namespace eventide::serde::flatbuffers
