#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "kota/support/expected_try.h"
#include "kota/codec/detail/backend.h"
#include "kota/codec/detail/codec.h"
#include "kota/codec/detail/common.h"
#include "kota/codec/detail/config.h"
#include "kota/codec/detail/narrow.h"
#include "kota/codec/toml/error.h"
#include "kota/codec/toml/serializer.h"

#if __has_include(<toml++/toml.hpp>)
#include "toml++/toml.hpp"
#else
#error "toml++/toml.hpp not found. Enable KOTA_CODEC_ENABLE_TOML or add tomlplusplus include paths."
#endif

namespace kota::codec::toml {

namespace detail {

using codec::detail::clean_t;
using codec::detail::remove_annotation_t;
using codec::detail::remove_optional_t;

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
constexpr bool root_table_v = (meta::reflectable_class<T> && !is_pair_v<T> && !is_tuple_v<T> &&
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

struct toml_source_adapter {
    using node_type = const ::toml::node*;

    static meta::type_kind kind_of(node_type node) {
        if(!node)
            return meta::type_kind::null;
        if(node->is_boolean())
            return meta::type_kind::boolean;
        if(node->is_integer())
            return meta::type_kind::int64;
        if(node->is_floating_point())
            return meta::type_kind::float64;
        if(node->is_string())
            return meta::type_kind::string;
        if(node->is_array())
            return meta::type_kind::array;
        if(node->is_table())
            return meta::type_kind::structure;
        return meta::type_kind::any;
    }

    template <typename Fn>
    static void for_each_field(node_type node, Fn&& fn) {
        if(!node)
            return;
        if(const auto* tbl = node->as_table()) {
            for(const auto& [k, v]: *tbl) {
                fn(std::string_view(k), &v);
            }
        }
    }

    template <typename Fn>
    static void for_each_element(node_type node, Fn&& fn) {
        if(!node)
            return;
        if(const auto* arr = node->as_array()) {
            std::size_t total = arr->size();
            for(std::size_t i = 0; i < total; ++i) {
                fn(i, total, arr->get(i));
            }
        }
    }
};

template <typename Config = config::default_config>
class Deserializer {
public:
    using config_type = Config;
    using error_type = toml::error;

    constexpr static auto backend_kind_v = backend_kind::streaming;
    constexpr static auto field_mode_v = field_mode::by_name;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    using status_t = result_t<void>;

    explicit Deserializer(const ::toml::table& root) :
        root_node(std::addressof(static_cast<const ::toml::node&>(root))) {}

    explicit Deserializer(const ::toml::node& root) : root_node(std::addressof(root)) {}

    explicit Deserializer(const ::toml::node* root) : root_node(root) {}

    [[nodiscard]] bool valid() const noexcept {
        return is_valid;
    }

    [[nodiscard]] error_type error() const noexcept {
        return last_error;
    }

    status_t finish() {
        if(!is_valid) {
            return std::unexpected(last_error);
        }
        if(!root_consumed) {
            return mark_invalid(error_kind::invalid_state);
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

        auto node = peek_node();
        if(!node) {
            return std::unexpected(node.error());
        }

        auto best = codec::select_variant_index<toml_source_adapter, config_type, Ts...>(*node);

        if(!best) {
            return mark_invalid(error_type::type_mismatch);
        }

        return codec::deserialize_variant_at<error_type>(*this, value, *best);
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

    template <codec::int_like T>
    status_t deserialize_int(T& value) {
        return read_and_narrow<std::int64_t>(
            value,
            [](const ::toml::node& node) -> result_t<std::int64_t> {
                auto parsed = node.value<std::int64_t>();
                if(!parsed.has_value()) {
                    return std::unexpected(error_kind::type_mismatch);
                }
                return *parsed;
            },
            [](auto p) {
                return codec::detail::narrow_int<T>(p, error_kind::number_out_of_range);
            });
    }

    template <codec::uint_like T>
    status_t deserialize_uint(T& value) {
        return read_and_narrow<std::int64_t>(
            value,
            [](const ::toml::node& node) -> result_t<std::int64_t> {
                auto parsed = node.value<std::int64_t>();
                if(!parsed.has_value()) {
                    return std::unexpected(error_kind::type_mismatch);
                }
                return *parsed;
            },
            [](std::int64_t p) -> std::expected<T, error_type> {
                if(p < 0) {
                    return std::unexpected(error_kind::number_out_of_range);
                }
                return codec::detail::narrow_uint<T>(static_cast<std::uint64_t>(p),
                                                     error_kind::number_out_of_range);
            });
    }

    template <codec::floating_like T>
    status_t deserialize_float(T& value) {
        return read_and_narrow<double>(
            value,
            [](const ::toml::node& node) -> result_t<double> {
                auto parsed = node.value<double>();
                if(!parsed.has_value()) {
                    return std::unexpected(error_kind::type_mismatch);
                }
                return *parsed;
            },
            [](auto p) {
                return codec::detail::narrow_float<T>(p, error_kind::number_out_of_range);
            });
    }

    status_t deserialize_char(char& value) {
        return read_and_narrow<std::string>(
            value,
            [](const ::toml::node& node) -> result_t<std::string> {
                auto parsed = node.value<std::string>();
                if(!parsed.has_value()) {
                    return std::unexpected(error_kind::type_mismatch);
                }
                return std::move(*parsed);
            },
            [](const std::string& p) {
                return codec::detail::narrow_char(std::string_view(p), error_kind::type_mismatch);
            });
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
        KOTA_EXPECTED_TRY(begin_array());
        value.clear();
        while(true) {
            KOTA_EXPECTED_TRY_V(auto has_next, next_element());
            if(!has_next) {
                break;
            }
            std::uint64_t byte_val = 0;
            KOTA_EXPECTED_TRY(deserialize_uint(byte_val));
            if(byte_val > 255U) {
                return mark_invalid(error_kind::number_out_of_range);
            }
            value.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(byte_val)));
        }
        return end_array();
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

    result_t<meta::type_kind> peek_kind() {
        auto node = peek_node();
        if(!node) {
            return std::unexpected(node.error());
        }
        return toml_source_adapter::kind_of(*node);
    }

    result_t<std::string> scan_object_field(std::string_view field_name) {
        auto node = peek_node();
        if(!node) {
            return std::unexpected(node.error());
        }
        if(*node == nullptr) {
            return mark_invalid(error_kind::type_mismatch);
        }
        const auto* table = (*node)->as_table();
        if(table == nullptr) {
            return mark_invalid(error_kind::type_mismatch);
        }
        auto it = table->find(field_name);
        if(it == table->cend()) {
            return std::unexpected(
                error_type::custom(std::format("missing field '{}'", field_name)));
        }
        auto val = it->second.template value<std::string_view>();
        if(!val.has_value()) {
            return mark_invalid(error_kind::type_mismatch);
        }
        return std::string(*val);
    }

    status_t begin_object() {
        KOTA_EXPECTED_TRY_V(auto table, open_as<::toml::table>());
        deser_frame frame;
        frame.table = table;
        frame.iter = table->cbegin();
        frame.end_iter = table->cend();
        deser_stack.push_back(std::move(frame));
        return {};
    }

    result_t<std::optional<std::string_view>> next_field() {
        if(!is_valid || deser_stack.empty()) {
            return mark_invalid(error_kind::invalid_state);
        }
        auto& frame = deser_stack.back();

        // Advance past the previous field (consumed by deserialization)
        if(frame.pending_node != nullptr) {
            ++frame.iter;
            frame.pending_node = nullptr;
        }

        if(frame.iter == frame.end_iter) {
            has_current_value = false;
            current_node = nullptr;
            return std::optional<std::string_view>(std::nullopt);
        }

        const auto& [key, node] = *frame.iter;
        frame.pending_node = std::addressof(node);
        current_node = frame.pending_node;
        has_current_value = true;
        return std::optional<std::string_view>(key.str());
    }

    status_t skip_field_value() {
        if(!is_valid || deser_stack.empty()) {
            return mark_invalid(error_kind::invalid_state);
        }
        auto& frame = deser_stack.back();
        ++frame.iter;
        frame.pending_node = nullptr;
        has_current_value = false;
        current_node = nullptr;
        return {};
    }

    status_t end_object() {
        if(!is_valid || deser_stack.empty()) {
            return mark_invalid(error_kind::invalid_state);
        }
        deser_stack.pop_back();
        has_current_value = false;
        current_node = nullptr;
        return {};
    }

    status_t begin_array() {
        KOTA_EXPECTED_TRY_V(auto arr, open_as<::toml::array>());
        array_stack.push_back({arr, 0});
        return {};
    }

    result_t<bool> next_element() {
        if(!is_valid || array_stack.empty()) {
            return mark_invalid(error_kind::invalid_state);
        }
        auto& frame = array_stack.back();
        if(frame.index >= frame.array->size()) {
            has_current_value = false;
            current_node = nullptr;
            return false;
        }
        current_node = std::addressof((*frame.array)[frame.index]);
        has_current_value = true;
        ++frame.index;
        return true;
    }

    status_t end_array() {
        if(!is_valid || array_stack.empty()) {
            return mark_invalid(error_kind::invalid_state);
        }
        array_stack.pop_back();
        has_current_value = false;
        current_node = nullptr;
        return {};
    }

private:
    template <typename Parsed, typename T, typename Fn, typename NarrowFn>
    status_t read_and_narrow(T& value, Fn&& fn, NarrowFn&& narrow_fn) {
        Parsed parsed{};
        KOTA_EXPECTED_TRY(read_scalar(parsed, std::forward<Fn>(fn)));
        auto narrowed = std::forward<NarrowFn>(narrow_fn)(parsed);
        if(!narrowed) {
            return mark_invalid(narrowed.error());
        }
        value = *narrowed;
        return {};
    }

    template <typename T, typename Reader>
    status_t read_scalar(T& out, Reader&& reader) {
        auto node = consume_node();
        if(!node) {
            return std::unexpected(node.error());
        }
        if(*node == nullptr) {
            return mark_invalid(error_kind::type_mismatch);
        }

        auto parsed = std::forward<Reader>(reader)(**node);
        if(!parsed) {
            return mark_invalid(parsed.error());
        }

        out = std::move(*parsed);
        return {};
    }

    result_t<const ::toml::node*> access_node(bool consume) {
        if(!is_valid) {
            return std::unexpected(last_error);
        }
        if(has_current_value) {
            last_accessed_node = current_node;
            return current_node;
        }
        if(root_consumed) {
            return mark_invalid(error_kind::invalid_state);
        }
        if(consume) {
            root_consumed = true;
        }
        last_accessed_node = root_node;
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
            return mark_invalid(error_kind::type_mismatch);
        }

        const auto* casted = [&]() -> const T* {
            if constexpr(std::same_as<T, ::toml::array>) {
                return (*node)->as_array();
            } else {
                return (*node)->as_table();
            }
        }();

        if(casted == nullptr) {
            return mark_invalid(error_kind::type_mismatch);
        }
        return casted;
    }

    static std::optional<codec::source_location> source_from_node(const ::toml::node* node) {
        if(!node) {
            return std::nullopt;
        }
        auto region = node->source();
        if(!static_cast<bool>(region.begin)) {
            return std::nullopt;
        }
        return codec::source_location{
            static_cast<std::size_t>(region.begin.line),
            static_cast<std::size_t>(region.begin.column),
            0,
        };
    }

    std::unexpected<error_type> mark_invalid(error_type error = error_type::invalid_state) {
        is_valid = false;
        if(last_error == error_type::invalid_state || error != error_type::invalid_state) {
            if(!error.location()) {
                if(auto loc = source_from_node(last_accessed_node)) {
                    error.set_location(*loc);
                }
            }
            last_error = error;
        }
        return std::unexpected(last_error);
    }

private:
    struct deser_frame {
        const ::toml::table* table = nullptr;
        ::toml::table::const_iterator iter{};
        ::toml::table::const_iterator end_iter{};
        const ::toml::node* pending_node = nullptr;
    };

    struct array_frame {
        const ::toml::array* array = nullptr;
        std::size_t index = 0;
    };

    bool is_valid = true;
    bool root_consumed = false;
    error_type last_error = error_type::invalid_state;
    const ::toml::node* root_node = nullptr;
    bool has_current_value = false;
    const ::toml::node* current_node = nullptr;
    const ::toml::node* last_accessed_node = nullptr;
    std::vector<deser_frame> deser_stack;
    std::vector<array_frame> array_stack;
};

template <typename Config = config::default_config, typename T>
auto from_toml(const ::toml::table& table, T& value) -> std::expected<void, error> {
    const auto* root = detail::select_root_node<T>(table);
    Deserializer<Config> deserializer(root);

    KOTA_EXPECTED_TRY(codec::deserialize(deserializer, value));
    KOTA_EXPECTED_TRY(deserializer.finish());
    return {};
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_toml(const ::toml::table& table) -> std::expected<T, error> {
    T value{};
    KOTA_EXPECTED_TRY(from_toml<Config>(table, value));
    return value;
}

static_assert(codec::deserializer_like<Deserializer<>>);

}  // namespace kota::codec::toml
