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

#include "eventide/serde/serde.h"
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

template <typename T>
struct remove_annotation {
    using type = std::remove_cvref_t<T>;
};

template <typename T>
    requires requires { typename std::remove_cvref_t<T>::annotated_type; }
struct remove_annotation<T> {
    using type = std::remove_cvref_t<typename std::remove_cvref_t<T>::annotated_type>;
};

template <typename T>
using remove_annotation_t = typename remove_annotation<T>::type;

template <typename T>
struct remove_optional {
    using type = std::remove_cvref_t<T>;
};

template <typename T>
struct remove_optional<std::optional<T>> {
    using type = std::remove_cvref_t<T>;
};

template <typename T>
using remove_optional_t = typename remove_optional<std::remove_cvref_t<T>>::type;

template <typename T>
using clean_t = remove_optional_t<remove_annotation_t<T>>;

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

class Deserializer {
public:
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

    class DeserializeObject {
    public:
        result_t<std::optional<std::string_view>> next_key() {
            if(!deserializer.valid()) {
                return std::unexpected(deserializer.current_error());
            }
            if(pending_value) {
                deserializer.mark_invalid(error_kind::invalid_state);
                return std::unexpected(deserializer.current_error());
            }
            if(index == entries.size()) {
                return std::optional<std::string_view>{};
            }

            pending_value = true;
            return std::optional<std::string_view>{entries[index].key};
        }

        status_t invalid_key(std::string_view /*key*/) {
            if(!deserializer.valid()) {
                return std::unexpected(deserializer.current_error());
            }
            if(!pending_value) {
                deserializer.mark_invalid(error_kind::invalid_state);
                return std::unexpected(deserializer.current_error());
            }

            ++index;
            pending_value = false;
            return {};
        }

        template <typename T>
        status_t deserialize_value(T& value) {
            if(!deserializer.valid()) {
                return std::unexpected(deserializer.current_error());
            }
            if(!pending_value) {
                deserializer.mark_invalid(error_kind::invalid_state);
                return std::unexpected(deserializer.current_error());
            }

            auto status = deserializer.deserialize_from_node(entries[index].value, value);
            if(!status) {
                return std::unexpected(status.error());
            }

            ++index;
            pending_value = false;
            return {};
        }

        status_t skip_value() {
            if(!deserializer.valid()) {
                return std::unexpected(deserializer.current_error());
            }
            if(!pending_value) {
                deserializer.mark_invalid(error_kind::invalid_state);
                return std::unexpected(deserializer.current_error());
            }

            ++index;
            pending_value = false;
            return {};
        }

        status_t end() {
            if(!deserializer.valid()) {
                return std::unexpected(deserializer.current_error());
            }

            if(pending_value) {
                auto status = skip_value();
                if(!status) {
                    return std::unexpected(status.error());
                }
            }

            index = entries.size();
            return {};
        }

    private:
        friend class Deserializer;

        struct object_entry {
            std::string_view key;
            const ::toml::node* value = nullptr;
        };

        explicit DeserializeObject(Deserializer& deserializer) : deserializer(deserializer) {}

        Deserializer& deserializer;
        std::vector<object_entry> entries;
        std::size_t index = 0;
        bool pending_value = false;
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
        static_assert((std::default_initializable<Ts> && ...),
                      "variant deserialization requires default-constructible alternatives");

        auto kind = peek_node_kind();
        if(!kind) {
            return std::unexpected(kind.error());
        }

        auto source = consume_node();
        if(!source) {
            return std::unexpected(source.error());
        }

        bool matched = false;
        bool considered = false;
        error_type last_error = error_type::type_mismatch;

        auto try_alternative = [&](auto type_tag) {
            if(matched) {
                return;
            }

            using alt_t = typename decltype(type_tag)::type;
            if(!variant_candidate_matches<alt_t>(*kind)) {
                return;
            }

            considered = true;
            auto status = deserialize_variant_candidate<alt_t>(*source, value);
            if(status) {
                matched = true;
            } else {
                last_error = status.error();
            }
        };

        (try_alternative(std::type_identity<Ts>{}), ...);

        if(!matched) {
            mark_invalid(considered ? last_error : error_type::type_mismatch);
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

        if(!std::in_range<T>(parsed)) {
            mark_invalid(error_kind::number_out_of_range);
            return std::unexpected(current_error());
        }

        value = static_cast<T>(parsed);
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
        if(!std::in_range<T>(unsigned_value)) {
            mark_invalid(error_kind::number_out_of_range);
            return std::unexpected(current_error());
        }

        value = static_cast<T>(unsigned_value);
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

        if constexpr(!std::same_as<T, double>) {
            const auto low = static_cast<long double>((std::numeric_limits<T>::lowest)());
            const auto high = static_cast<long double>((std::numeric_limits<T>::max)());
            const auto value_as_long_double = static_cast<long double>(parsed);
            if(value_as_long_double < low || value_as_long_double > high) {
                mark_invalid(error_kind::number_out_of_range);
                return std::unexpected(current_error());
            }
        }

        value = static_cast<T>(parsed);
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

        if(text.size() != 1U) {
            mark_invalid(error_kind::type_mismatch);
            return std::unexpected(current_error());
        }

        value = text.front();
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
        auto seq = deserialize_seq(std::nullopt);
        if(!seq) {
            return std::unexpected(seq.error());
        }

        value.clear();
        while(true) {
            auto has_next_value = seq->has_next();
            if(!has_next_value) {
                return std::unexpected(has_next_value.error());
            }
            if(!*has_next_value) {
                break;
            }

            std::uint64_t byte = 0;
            auto status = seq->deserialize_element(byte);
            if(!status) {
                return std::unexpected(status.error());
            }
            if(byte > static_cast<std::uint64_t>((std::numeric_limits<std::uint8_t>::max)())) {
                mark_invalid(error_kind::number_out_of_range);
                return std::unexpected(current_error());
            }

            value.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(byte)));
        }
        return seq->end();
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
                typename DeserializeMap::object_entry{key.str(), std::addressof(value)});
        }
        return object;
    }

    result_t<DeserializeStruct> deserialize_struct(std::string_view /*name*/, std::size_t /*len*/) {
        return deserialize_map(std::nullopt);
    }

    result_t<::toml::table> capture_table() {
        auto node = consume_node();
        if(!node) {
            return std::unexpected(node.error());
        }
        if(*node == nullptr) {
            mark_invalid(error_kind::type_mismatch);
            return std::unexpected(current_error());
        }

        const auto* table = (*node)->as_table();
        if(table == nullptr) {
            mark_invalid(error_kind::type_mismatch);
            return std::unexpected(current_error());
        }
        return *table;
    }

    result_t<::toml::array> capture_array() {
        auto node = consume_node();
        if(!node) {
            return std::unexpected(node.error());
        }
        if(*node == nullptr) {
            mark_invalid(error_kind::type_mismatch);
            return std::unexpected(current_error());
        }

        const auto* array = (*node)->as_array();
        if(array == nullptr) {
            mark_invalid(error_kind::type_mismatch);
            return std::unexpected(current_error());
        }
        return *array;
    }

private:
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

    template <typename T>
    constexpr static bool variant_candidate_matches(node_kind kind) {
        using U = std::remove_cvref_t<T>;

        if constexpr(serde::annotated_type<U>) {
            return variant_candidate_matches<typename U::annotated_type>(kind);
        } else if constexpr(is_specialization_of<std::optional, U>) {
            if(kind == node_kind::none) {
                return true;
            }
            return variant_candidate_matches<typename U::value_type>(kind);
        } else if constexpr(std::same_as<U, std::nullptr_t>) {
            return kind == node_kind::none;
        } else if constexpr(serde::bool_like<U>) {
            return kind == node_kind::boolean;
        } else if constexpr(serde::int_like<U> || serde::uint_like<U>) {
            return kind == node_kind::integer;
        } else if constexpr(serde::floating_like<U>) {
            return kind == node_kind::integer || kind == node_kind::floating;
        } else if constexpr(serde::char_like<U> || std::same_as<U, std::string> ||
                            std::derived_from<U, std::string>) {
            return kind == node_kind::string;
        } else if constexpr(std::same_as<U, std::vector<std::byte>>) {
            return kind == node_kind::array;
        } else if constexpr(is_pair_v<U> || is_tuple_v<U>) {
            return kind == node_kind::array;
        } else if constexpr(std::ranges::input_range<U>) {
            constexpr auto format_kind = eventide::format_kind<U>;
            if constexpr(format_kind == range_format::map) {
                return kind == node_kind::table;
            } else if constexpr(format_kind == range_format::sequence ||
                                format_kind == range_format::set) {
                return kind == node_kind::array;
            } else {
                return true;
            }
        } else if constexpr(refl::reflectable_class<U>) {
            return kind == node_kind::table;
        } else {
            return true;
        }
    }

    template <typename Alt, typename... Ts>
    static auto deserialize_variant_candidate(const ::toml::node* source,
                                              std::variant<Ts...>& value) -> status_t {
        Alt candidate{};
        Deserializer probe(source);
        if(!probe.valid()) {
            return std::unexpected(probe.error());
        }

        auto status = serde::deserialize(probe, candidate);
        if(!status) {
            return std::unexpected(status.error());
        }

        auto finished = probe.finish();
        if(!finished) {
            return std::unexpected(finished.error());
        }

        value = std::move(candidate);
        return {};
    }

    result_t<const ::toml::node*> peek_node() {
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
        return root_node;
    }

    result_t<const ::toml::node*> consume_node() {
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
        root_consumed = true;
        return root_node;
    }

    result_t<const ::toml::array*> open_array() {
        auto node = consume_node();
        if(!node) {
            return std::unexpected(node.error());
        }
        if(*node == nullptr) {
            mark_invalid(error_kind::type_mismatch);
            return std::unexpected(current_error());
        }

        const auto* array = (*node)->as_array();
        if(array == nullptr) {
            mark_invalid(error_kind::type_mismatch);
            return std::unexpected(current_error());
        }
        return array;
    }

    result_t<const ::toml::table*> open_table() {
        auto node = consume_node();
        if(!node) {
            return std::unexpected(node.error());
        }
        if(*node == nullptr) {
            mark_invalid(error_kind::type_mismatch);
            return std::unexpected(current_error());
        }

        const auto* table = (*node)->as_table();
        if(table == nullptr) {
            mark_invalid(error_kind::type_mismatch);
            return std::unexpected(current_error());
        }
        return table;
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

template <typename T>
auto from_toml(const ::toml::table& table, T& value) -> std::expected<void, error_kind> {
    const auto* root = detail::select_root_node<T>(table);
    Deserializer deserializer(root);

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

template <typename T>
    requires std::default_initializable<T>
auto from_toml(const ::toml::table& table) -> std::expected<T, error_kind> {
    T value{};
    auto status = from_toml(table, value);
    if(!status) {
        return std::unexpected(status.error());
    }
    return value;
}

static_assert(serde::deserializer_like<Deserializer>);

}  // namespace eventide::serde::toml
