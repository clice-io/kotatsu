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
#include <variant>
#include <vector>

#include "eventide/serde/serde/config.h"
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
constexpr bool root_table_v =
    refl::reflectable_class<T> || is_map_like_v<T> || std::same_as<T, ::toml::table>;

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

    class DeserializeArray {
    public:
        result_t<bool> has_next() {
            if(!deserializer.valid()) {
                return std::unexpected(deserializer.current_error());
            }
            if(array == nullptr) {
                deserializer.mark_invalid(error_kind::invalid_state);
                return std::unexpected(deserializer.current_error());
            }
            return index < array->size();
        }

        template <typename T>
        status_t deserialize_element(T& value) {
            auto has_next_value = has_next();
            if(!has_next_value) {
                return std::unexpected(has_next_value.error());
            }
            if(!*has_next_value) {
                deserializer.mark_invalid(error_kind::invalid_state);
                return std::unexpected(deserializer.current_error());
            }

            const auto* node = std::addressof((*array)[index]);
            auto status = deserializer.deserialize_from_node(node, value);
            if(!status) {
                return std::unexpected(status.error());
            }

            ++index;
            ++consumed_count;
            return {};
        }

        status_t skip_element() {
            auto has_next_value = has_next();
            if(!has_next_value) {
                return std::unexpected(has_next_value.error());
            }
            if(!*has_next_value) {
                deserializer.mark_invalid(error_kind::invalid_state);
                return std::unexpected(deserializer.current_error());
            }

            ++index;
            ++consumed_count;
            return {};
        }

        status_t end() {
            if(!deserializer.valid()) {
                return std::unexpected(deserializer.current_error());
            }

            if(strict_length) {
                if(consumed_count != expected_length) {
                    deserializer.mark_invalid(error_kind::invalid_state);
                    return std::unexpected(deserializer.current_error());
                }

                auto has_next_value = has_next();
                if(!has_next_value) {
                    return std::unexpected(has_next_value.error());
                }
                if(*has_next_value) {
                    deserializer.mark_invalid(error_kind::trailing_content);
                    return std::unexpected(deserializer.current_error());
                }
                return {};
            }

            if(array != nullptr) {
                index = array->size();
            }
            return {};
        }

    private:
        friend class Deserializer;

        DeserializeArray(Deserializer& deserializer,
                         const ::toml::array* array,
                         std::size_t expected_length,
                         bool strict_length) :
            deserializer(deserializer), array(array), expected_length(expected_length),
            strict_length(strict_length) {}

        Deserializer& deserializer;
        const ::toml::array* array = nullptr;
        std::size_t index = 0;
        std::size_t expected_length = 0;
        std::size_t consumed_count = 0;
        bool strict_length = false;
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

    template <typename... Ts>
    status_t deserialize_variant(std::variant<Ts...>& value) {
        auto kind = peek_node_kind();
        if(!kind) {
            return std::unexpected(kind.error());
        }

        auto source = consume_node();
        if(!source) {
            return std::unexpected(source.error());
        }

        auto result = serde::try_variant_dispatch<Deserializer>(*source,
                                                                map_to_type_hint(*kind),
                                                                value,
                                                                error_type::type_mismatch);
        if(!result) {
            mark_invalid(result.error());
            return std::unexpected(current_error());
        }
        return {};
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
        auto array = open_array();
        if(!array) {
            return std::unexpected(array.error());
        }
        return DeserializeSeq(*this, *array, len.value_or(0), false);
    }

    result_t<DeserializeTuple> deserialize_tuple(std::size_t len) {
        auto array = open_array();
        if(!array) {
            return std::unexpected(array.error());
        }
        return DeserializeTuple(*this, *array, len, true);
    }

    result_t<DeserializeMap> deserialize_map(std::optional<std::size_t> /*len*/) {
        auto table = open_table();
        if(!table) {
            return std::unexpected(table.error());
        }

        DeserializeMap object(*this);
        object.entries.reserve((*table)->size());
        for(const auto& [key, value]: **table) {
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
    friend class serde::detail::IndexedObjectDeserializer<Deserializer, const ::toml::node*>;

    /// Bridge method for shared deserialize helpers.
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

    auto status = serde::deserialize(deserializer, value);
    if(!status) {
        return std::unexpected(status.error());
    }

    auto finished = deserializer.finish();
    if(!finished) {
        return std::unexpected(finished.error());
    }
    return {};
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_toml(const ::toml::table& table) -> std::expected<T, error_kind> {
    T value{};
    auto status = from_toml<Config>(table, value);
    if(!status) {
        return std::unexpected(status.error());
    }
    return value;
}

static_assert(serde::deserializer_like<Deserializer<>>);

}  // namespace eventide::serde::toml
