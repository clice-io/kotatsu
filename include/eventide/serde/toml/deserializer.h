#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "eventide/common/expected_try.h"
#include "eventide/serde/serde/config.h"
#include "eventide/serde/schema/match.h"
#include "eventide/serde/serde/serde.h"
#include "eventide/serde/serde/utils/backend_helpers.h"
#include "eventide/serde/serde/utils/common.h"
#include "eventide/serde/serde/utils/narrow.h"
#include "eventide/serde/toml/error.h"
#include "eventide/serde/toml/serializer.h"

#if __has_include(<toml++/toml.hpp>)
#include <toml++/toml.hpp>
#else
#error                                                                                             \
    "toml++/toml.hpp not found. Enable EVENTIDE_SERDE_ENABLE_TOML or add tomlplusplus include paths."
#endif

namespace eventide::serde::toml {

namespace detail {

using serde::detail::clean_t;
using serde::detail::remove_annotation_t;
using serde::detail::remove_optional_t;

template <typename T>
consteval bool is_map_like() {
    if constexpr(std::ranges::input_range<T>) {
        return format_kind<T> == range_format::map;
    } else {
        return false;
    }
}

template <typename T>
constexpr bool is_map_like_v = is_map_like<T>();

template <typename T>
constexpr bool root_table_v = (refl::reflectable_class<T> && !is_pair_v<T> && !is_tuple_v<T> &&
                               !std::ranges::input_range<T>) ||
                              is_map_like_v<T> || std::same_as<T, ::toml::table>;

template <typename T>
auto select_root_node(const ::toml::table& table) -> const ::toml::node* {
    using U = std::remove_cvref_t<T>;

    if constexpr(is_specialization_of<std::optional, U>) {
        if(table.empty()) {
            return nullptr;
        }
        using value_t = clean_t<U>;
        if constexpr(root_table_v<value_t>) {
            return std::addressof(static_cast<const ::toml::node&>(table));
        } else {
            return table.get(boxed_root_key);
        }
    } else if constexpr(root_table_v<clean_t<U>>) {
        return std::addressof(static_cast<const ::toml::node&>(table));
    } else {
        return table.get(boxed_root_key);
    }
}

}  // namespace detail

template <typename Config = config::default_config>
class Deserializer {
public:
    using config_type = Config;
    using error_type = error_kind;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    using status_t = result_t<void>;

    class DeserializeArray :
        public serde::detail::IndexedArrayDeserializer<Deserializer, const ::toml::array*> {
        using Base = serde::detail::IndexedArrayDeserializer<Deserializer, const ::toml::array*>;
        friend class Deserializer;

        DeserializeArray(Deserializer& deserializer,
                         const ::toml::array* array,
                         std::size_t expected_length,
                         bool strict_length) :
            Base(deserializer, array, array ? array->size() : 0, expected_length, strict_length) {}
    };

    class DeserializeObject :
        public serde::detail::IndexedObjectDeserializer<Deserializer, const ::toml::node*> {
        using Base = serde::detail::IndexedObjectDeserializer<Deserializer, const ::toml::node*>;
        friend class Deserializer;

        explicit DeserializeObject(Deserializer& deserializer) : Base(deserializer) {}
    };

    using DeserializeSeq = DeserializeArray;
    using DeserializeTuple = DeserializeArray;
    using DeserializeMap = DeserializeObject;
    using DeserializeStruct = DeserializeObject;

    explicit Deserializer(const ::toml::table& root) :
        root_node(std::addressof(static_cast<const ::toml::node&>(root))) {}

    explicit Deserializer(const ::toml::node& root) : root_node(std::addressof(root)) {}

    explicit Deserializer(const ::toml::node* root) : root_node(root) {}

    [[nodiscard]] bool valid() const noexcept {
        return is_valid;
    }

    [[nodiscard]] error_type error() const noexcept {
        return current_error();
    }

    status_t finish() {
        if(!is_valid) {
            return std::unexpected(current_error());
        }
        if(!root_consumed) {
            mark_invalid(error_kind::invalid_state);
            return std::unexpected(current_error());
        }
        return {};
    }

    result_t<bool> deserialize_none() {
        auto node = peek_node();
        if(!node) {
            return std::unexpected(node.error());
        }

        const bool is_none = (*node == nullptr);
        if(is_none && !has_current_value) {
            root_consumed = true;
        }
        return is_none;
    }

    result_t<const ::toml::node*> consume_variant_source() {
        return consume_node();
    }

    template <typename... Ts>
    result_t<void> deserialize_variant(std::variant<Ts...>& v) {
        auto source_result = consume_node();
        if(!source_result) return std::unexpected(source_result.error());
        auto source = *source_result;

        auto node = to_schema_node(source);
        using config_t = Config;
        return serde::schema::untagged_dispatch<Deserializer, config_t, Ts...>(
            v, node, [&]() -> Deserializer { return Deserializer(source); });
    }

    status_t deserialize_bool(bool& value) {
        return read_scalar(value, [](const ::toml::node& node) -> result_t<bool> {
            auto parsed = node.value<bool>();
            if(!parsed.has_value()) {
                return std::unexpected(error_kind::type_mismatch);
            }
            return *parsed;
        });
    }

    template <serde::int_like T>
    status_t deserialize_int(T& value) {
        std::int64_t parsed = 0;
        auto status = read_scalar(parsed, [](const ::toml::node& node) -> result_t<std::int64_t> {
            auto parsed = node.value<std::int64_t>();
            if(!parsed.has_value()) {
                return std::unexpected(error_kind::type_mismatch);
            }
            return *parsed;
        });
        if(!status) {
            return std::unexpected(status.error());
        }

        auto narrowed = serde::detail::narrow_int<T>(parsed, error_kind::number_out_of_range);
        if(!narrowed) {
            mark_invalid(narrowed.error());
            return std::unexpected(current_error());
        }

        value = *narrowed;
        return {};
    }

    template <serde::uint_like T>
    status_t deserialize_uint(T& value) {
        std::int64_t parsed = 0;
        auto status = read_scalar(parsed, [](const ::toml::node& node) -> result_t<std::int64_t> {
            auto parsed = node.value<std::int64_t>();
            if(!parsed.has_value()) {
                return std::unexpected(error_kind::type_mismatch);
            }
            return *parsed;
        });
        if(!status) {
            return std::unexpected(status.error());
        }

        if(parsed < 0) {
            mark_invalid(error_kind::number_out_of_range);
            return std::unexpected(current_error());
        }

        const auto unsigned_value = static_cast<std::uint64_t>(parsed);
        auto narrowed =
            serde::detail::narrow_uint<T>(unsigned_value, error_kind::number_out_of_range);
        if(!narrowed) {
            mark_invalid(narrowed.error());
            return std::unexpected(current_error());
        }

        value = *narrowed;
        return {};
    }

    template <serde::floating_like T>
    status_t deserialize_float(T& value) {
        double parsed = 0.0;
        auto status = read_scalar(parsed, [](const ::toml::node& node) -> result_t<double> {
            auto parsed = node.value<double>();
            if(!parsed.has_value()) {
                return std::unexpected(error_kind::type_mismatch);
            }
            return *parsed;
        });
        if(!status) {
            return std::unexpected(status.error());
        }

        auto narrowed = serde::detail::narrow_float<T>(parsed, error_kind::number_out_of_range);
        if(!narrowed) {
            mark_invalid(narrowed.error());
            return std::unexpected(current_error());
        }

        value = *narrowed;
        return {};
    }

    status_t deserialize_char(char& value) {
        std::string text;
        auto status = read_scalar(text, [](const ::toml::node& node) -> result_t<std::string> {
            auto parsed = node.value<std::string>();
            if(!parsed.has_value()) {
                return std::unexpected(error_kind::type_mismatch);
            }
            return std::move(*parsed);
        });
        if(!status) {
            return std::unexpected(status.error());
        }

        auto narrowed =
            serde::detail::narrow_char(std::string_view(text), error_kind::type_mismatch);
        if(!narrowed) {
            mark_invalid(narrowed.error());
            return std::unexpected(current_error());
        }

        value = *narrowed;
        return {};
    }

    status_t deserialize_str(std::string& value) {
        return read_scalar(value, [](const ::toml::node& node) -> result_t<std::string> {
            auto parsed = node.value<std::string>();
            if(!parsed.has_value()) {
                return std::unexpected(error_kind::type_mismatch);
            }
            return std::move(*parsed);
        });
    }

    status_t deserialize_bytes(std::vector<std::byte>& value) {
        return serde::detail::deserialize_bytes_from_seq(*this, value);
    }

    result_t<DeserializeSeq> deserialize_seq(std::optional<std::size_t> len) {
        ET_EXPECTED_TRY_V(auto array, open_array());
        return DeserializeSeq(*this, array, len.value_or(0), false);
    }

    result_t<DeserializeTuple> deserialize_tuple(std::size_t len) {
        ET_EXPECTED_TRY_V(auto array, open_array());
        return DeserializeTuple(*this, array, len, true);
    }

    result_t<DeserializeMap> deserialize_map(std::optional<std::size_t> /*len*/) {
        ET_EXPECTED_TRY_V(auto table, open_table());

        DeserializeMap object(*this);
        object.entries.reserve(table->size());
        for(const auto& [key, value]: *table) {
            object.entries.push_back(
                typename DeserializeMap::entry{key.str(), std::addressof(value)});
        }
        return object;
    }

    result_t<DeserializeStruct> deserialize_struct(std::string_view /*name*/, std::size_t /*len*/) {
        return deserialize_map(std::nullopt);
    }

    result_t<::toml::table> capture_table() {
        auto table = open_as<::toml::table>();
        if(!table) {
            return std::unexpected(table.error());
        }
        return **table;
    }

    result_t<::toml::array> capture_array() {
        auto array = open_as<::toml::array>();
        if(!array) {
            return std::unexpected(array.error());
        }
        return **array;
    }

private:
    friend class serde::detail::IndexedArrayDeserializer<Deserializer, const ::toml::array*>;
    friend class serde::detail::IndexedObjectDeserializer<Deserializer, const ::toml::node*>;

    /// Bridge method for shared array deserialize helpers.
    template <typename T>
    status_t deserialize_element_value(const ::toml::array* arr, std::size_t idx, T& out) {
        return deserialize_from_node(std::addressof((*arr)[idx]), out);
    }

    /// Bridge method for shared object deserialize helpers.
    template <typename T>
    status_t deserialize_entry_value(const ::toml::node* value, T& out) {
        return deserialize_from_node(value, out);
    }

    enum class node_kind : std::uint8_t {
        none,
        boolean,
        integer,
        floating,
        string,
        array,
        table,
        unknown,
    };

    template <typename T, typename Reader>
    status_t read_scalar(T& out, Reader&& reader) {
        auto node = consume_node();
        if(!node) {
            return std::unexpected(node.error());
        }
        if(*node == nullptr) {
            mark_invalid(error_kind::type_mismatch);
            return std::unexpected(current_error());
        }

        auto parsed = std::forward<Reader>(reader)(**node);
        if(!parsed) {
            mark_invalid(parsed.error());
            return std::unexpected(current_error());
        }

        out = std::move(*parsed);
        return {};
    }

    template <typename T>
    status_t deserialize_from_node(const ::toml::node* node, T& out) {
        struct value_scope {
            value_scope(Deserializer& deserializer, const ::toml::node* value) :
                deserializer(deserializer), previous_has_current(deserializer.has_current_value),
                previous_current(deserializer.current_node) {
                deserializer.current_node = value;
                deserializer.has_current_value = true;
            }

            ~value_scope() {
                deserializer.current_node = previous_current;
                deserializer.has_current_value = previous_has_current;
            }

            Deserializer& deserializer;
            bool previous_has_current;
            const ::toml::node* previous_current;
        };

        value_scope scope(*this, node);
        return serde::deserialize(*this, out);
    }

    result_t<node_kind> peek_node_kind() {
        auto node = peek_node();
        if(!node) {
            return std::unexpected(node.error());
        }
        return classify_node(*node);
    }

    static auto classify_node(const ::toml::node* node) -> node_kind {
        if(node == nullptr) {
            return node_kind::none;
        }
        if(node->is_boolean()) {
            return node_kind::boolean;
        }
        if(node->is_integer()) {
            return node_kind::integer;
        }
        if(node->is_floating_point()) {
            return node_kind::floating;
        }
        if(node->is_string()) {
            return node_kind::string;
        }
        if(node->is_array()) {
            return node_kind::array;
        }
        if(node->is_table()) {
            return node_kind::table;
        }
        return node_kind::unknown;
    }

    static serde::type_hint map_to_type_hint(node_kind kind) {
        switch(kind) {
            case node_kind::none: return serde::type_hint::null_like;
            case node_kind::boolean: return serde::type_hint::boolean;
            case node_kind::integer: return serde::type_hint::integer;
            case node_kind::floating: return serde::type_hint::floating;
            case node_kind::string: return serde::type_hint::string;
            case node_kind::array: return serde::type_hint::array;
            case node_kind::table: return serde::type_hint::object;
            default: return serde::type_hint::any;
        }
    }

    static serde::type_hint node_to_hint(const ::toml::node* n) {
        if(!n) return serde::type_hint::null_like;
        return map_to_type_hint(classify_node(n));
    }

    static serde::schema::schema_node to_schema_node(const ::toml::node* source) {
        serde::schema::schema_node node;
        node.hints = node_to_hint(source);

        if(source && source->is_table()) {
            auto& table = *source->as_table();
            for(auto& [k, v]: table) {
                node.fields.push_back({
                    std::string(k),
                    node_to_hint(&v),
                });
            }
        }
        return node;
    }

    result_t<const ::toml::node*> access_node(bool consume) {
        if(!is_valid) {
            return std::unexpected(current_error());
        }
        if(has_current_value) {
            return current_node;
        }
        if(root_consumed) {
            mark_invalid(error_kind::invalid_state);
            return std::unexpected(current_error());
        }
        if(consume) {
            root_consumed = true;
        }
        return root_node;
    }

    result_t<const ::toml::node*> peek_node() {
        return access_node(false);
    }

    result_t<const ::toml::node*> consume_node() {
        return access_node(true);
    }

    template <typename T>
    result_t<const T*> open_as() {
        auto node = consume_node();
        if(!node) {
            return std::unexpected(node.error());
        }
        if(*node == nullptr) {
            mark_invalid(error_kind::type_mismatch);
            return std::unexpected(current_error());
        }

        const auto* casted = [&]() -> const T* {
            if constexpr(std::same_as<T, ::toml::array>) {
                return (*node)->as_array();
            } else {
                return (*node)->as_table();
            }
        }();

        if(casted == nullptr) {
            mark_invalid(error_kind::type_mismatch);
            return std::unexpected(current_error());
        }
        return casted;
    }

    result_t<const ::toml::array*> open_array() {
        return open_as<::toml::array>();
    }

    result_t<const ::toml::table*> open_table() {
        return open_as<::toml::table>();
    }

    void mark_invalid(error_type error = error_type::invalid_state) {
        is_valid = false;
        if(last_error == error_type::invalid_state || error != error_type::invalid_state) {
            last_error = error;
        }
    }

    [[nodiscard]] error_type current_error() const noexcept {
        return last_error;
    }

private:
    bool is_valid = true;
    bool root_consumed = false;
    error_type last_error = error_type::invalid_state;
    const ::toml::node* root_node = nullptr;
    bool has_current_value = false;
    const ::toml::node* current_node = nullptr;
};

template <typename Config = config::default_config, typename T>
auto from_toml(const ::toml::table& table, T& value) -> std::expected<void, error_kind> {
    const auto* root = detail::select_root_node<T>(table);
    Deserializer<Config> deserializer(root);

    ET_EXPECTED_TRY(serde::deserialize(deserializer, value));
    ET_EXPECTED_TRY(deserializer.finish());
    return {};
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_toml(const ::toml::table& table) -> std::expected<T, error_kind> {
    T value{};
    ET_EXPECTED_TRY(from_toml<Config>(table, value));
    return value;
}

static_assert(serde::deserializer_like<Deserializer<>>);

}  // namespace eventide::serde::toml
