#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "kota/support/expected_try.h"
#include "kota/codec/codec.h"
#include "kota/codec/config.h"
#include "kota/codec/content/dom.h"
#include "kota/codec/content/error.h"
#include "kota/codec/detail/backend_helpers.h"

namespace kota::codec::content {

template <typename Config = config::default_config>
class Serializer {
public:
    using config_type = Config;
    using value_type = void;
    using error_type = content::error_kind;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    using status_t = result_t<void>;

    using SerializeArray = codec::detail::SerializeArray<Serializer<Config>>;
    using SerializeObject = codec::detail::SerializeObject<Serializer<Config>>;

    using SerializeSeq = SerializeArray;
    using SerializeTuple = SerializeArray;
    using SerializeMap = SerializeObject;
    using SerializeStruct = SerializeObject;

    Serializer() = default;

    Serializer(const Serializer&) = delete;
    Serializer(Serializer&&) = delete;
    auto operator=(const Serializer&) -> Serializer& = delete;
    auto operator=(Serializer&&) -> Serializer& = delete;

    [[nodiscard]] bool valid() const noexcept {
        return is_valid;
    }

    [[nodiscard]] error_type error() const noexcept {
        return last_error;
    }

    result_t<content::Value> dom_value() const {
        if(!is_valid) {
            return std::unexpected(last_error);
        }
        if(!root_is_written || !stack.empty()) {
            return std::unexpected(error_type::invalid_state);
        }
        return root;
    }

    result_t<content::Value> take_dom_value() {
        if(!is_valid) {
            return std::unexpected(last_error);
        }
        if(!root_is_written || !stack.empty()) {
            return std::unexpected(error_type::invalid_state);
        }
        root_is_written = false;
        return std::move(root);
    }

    result_t<value_type> serialize_null() {
        return append_value(content::Value(nullptr));
    }

    template <typename T>
    result_t<value_type> serialize_some(const T& value) {
        return codec::serialize(*this, value);
    }

    template <typename... Ts>
    result_t<value_type> serialize_variant(const std::variant<Ts...>& value) {
        return std::visit(
            [&](const auto& item) -> result_t<value_type> { return codec::serialize(*this, item); },
            value);
    }

    result_t<value_type> serialize_bool(bool v) {
        return append_value(content::Value(v));
    }

    result_t<value_type> serialize_int(std::int64_t v) {
        return append_value(content::Value(v));
    }

    result_t<value_type> serialize_uint(std::uint64_t v) {
        return append_value(content::Value(v));
    }

    result_t<value_type> serialize_float(double v) {
        if(std::isfinite(v)) {
            return append_value(content::Value(v));
        }
        return serialize_null();
    }

    result_t<value_type> serialize_char(char v) {
        return append_value(content::Value(std::string(1, v)));
    }

    result_t<value_type> serialize_str(std::string_view v) {
        return append_value(content::Value(v));
    }

    result_t<value_type> serialize_bytes(std::string_view value) {
        KOTA_EXPECTED_TRY_V(auto seq, serialize_seq(value.size()));
        for(unsigned char byte: value) {
            KOTA_EXPECTED_TRY(seq.serialize_element(static_cast<std::uint64_t>(byte)));
        }
        return seq.end();
    }

    result_t<value_type> serialize_bytes(std::span<const std::byte> value) {
        KOTA_EXPECTED_TRY_V(auto seq, serialize_seq(value.size()));
        for(std::byte byte: value) {
            KOTA_EXPECTED_TRY(seq.serialize_element(
                static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(byte))));
        }
        return seq.end();
    }

    result_t<SerializeSeq> serialize_seq(std::optional<std::size_t> /*len*/) {
        KOTA_EXPECTED_TRY(begin_array());
        return SerializeSeq(*this);
    }

    result_t<SerializeTuple> serialize_tuple(std::size_t /*len*/) {
        KOTA_EXPECTED_TRY(begin_array());
        return SerializeTuple(*this);
    }

    result_t<SerializeMap> serialize_map(std::optional<std::size_t> /*len*/) {
        KOTA_EXPECTED_TRY(begin_object());
        return SerializeMap(*this);
    }

    result_t<SerializeStruct> serialize_struct(std::string_view /*name*/, std::size_t /*len*/) {
        KOTA_EXPECTED_TRY(begin_object());
        return SerializeStruct(*this);
    }

    result_t<value_type> append_dom_value(const content::Value& value) {
        return append_value(value);
    }

    result_t<value_type> append_dom_value(content::Value&& value) {
        return append_value(std::move(value));
    }

    result_t<value_type> append_dom_value(const content::Array& value) {
        return append_value(content::Value(value));
    }

    result_t<value_type> append_dom_value(content::Array&& value) {
        return append_value(content::Value(std::move(value)));
    }

    result_t<value_type> append_dom_value(const content::Object& value) {
        return append_value(content::Value(value));
    }

    result_t<value_type> append_dom_value(content::Object&& value) {
        return append_value(content::Value(std::move(value)));
    }

private:
    friend class codec::detail::SerializeArray<Serializer<Config>>;
    friend class codec::detail::SerializeObject<Serializer<Config>>;

    struct frame {
        content::Array* array = nullptr;
        content::Object* object = nullptr;
        std::string pending_key;
        bool has_pending_key = false;
    };

    status_t begin_array() {
        if(!is_valid) {
            return std::unexpected(last_error);
        }
        KOTA_EXPECTED_TRY(append_value(content::Value(content::Array{})));
        content::Array* arr = last_placed_array();
        if(arr == nullptr) {
            return mark_invalid();
        }
        stack.push_back(
            frame{.array = arr, .object = nullptr, .pending_key = {}, .has_pending_key = false});
        return {};
    }

    status_t end_array() {
        if(!is_valid || stack.empty() || stack.back().array == nullptr) {
            return mark_invalid();
        }
        stack.pop_back();
        return {};
    }

    status_t begin_object() {
        if(!is_valid) {
            return std::unexpected(last_error);
        }
        KOTA_EXPECTED_TRY(append_value(content::Value(content::Object{})));
        content::Object* obj = last_placed_object();
        if(obj == nullptr) {
            return mark_invalid();
        }
        stack.push_back(
            frame{.array = nullptr, .object = obj, .pending_key = {}, .has_pending_key = false});
        return {};
    }

    status_t end_object() {
        if(!is_valid || stack.empty() || stack.back().object == nullptr ||
           stack.back().has_pending_key) {
            return mark_invalid();
        }
        stack.pop_back();
        return {};
    }

    status_t key(std::string_view key_name) {
        if(!is_valid || stack.empty() || stack.back().object == nullptr) {
            return mark_invalid();
        }
        auto& f = stack.back();
        if(f.has_pending_key) {
            return mark_invalid();
        }
        f.pending_key.assign(key_name.data(), key_name.size());
        f.has_pending_key = true;
        return {};
    }

    status_t append_value(content::Value v) {
        if(!is_valid) {
            return std::unexpected(last_error);
        }
        if(stack.empty()) {
            if(root_is_written) {
                return mark_invalid();
            }
            root = std::move(v);
            root_is_written = true;
            return {};
        }
        auto& f = stack.back();
        if(f.array != nullptr) {
            f.array->push_back(std::move(v));
            return {};
        }
        if(f.object == nullptr || !f.has_pending_key) {
            return mark_invalid();
        }
        f.object->insert(std::move(f.pending_key), std::move(v));
        f.pending_key.clear();
        f.has_pending_key = false;
        return {};
    }

    content::Array* last_placed_array() {
        content::Value* slot = last_placed_slot();
        return slot != nullptr ? slot->get_array() : nullptr;
    }

    content::Object* last_placed_object() {
        content::Value* slot = last_placed_slot();
        return slot != nullptr ? slot->get_object() : nullptr;
    }

    content::Value* last_placed_slot() {
        if(stack.empty()) {
            return &root;
        }
        auto& f = stack.back();
        if(f.array != nullptr) {
            return f.array->empty() ? nullptr : &(*f.array)[f.array->size() - 1];
        }
        if(f.object != nullptr) {
            return f.object->empty() ? nullptr : &f.object->back_value();
        }
        return nullptr;
    }

    std::unexpected<error_type> mark_invalid(error_type err = error_type::invalid_state) {
        is_valid = false;
        if(last_error == error_type::invalid_state || err != error_type::invalid_state) {
            last_error = err;
        }
        return std::unexpected(last_error);
    }

    content::Value root;
    std::vector<frame> stack;
    bool is_valid = true;
    bool root_is_written = false;
    error_type last_error = error_type::invalid_state;
};

static_assert(codec::serializer_like<Serializer<>>);

}  // namespace kota::codec::content

namespace kota::codec {

// --- Generic Value/Array/Object serialization: dispatch across any Serializer. ---

template <typename T>
concept content_dom_type = std::same_as<T, content::Value> || std::same_as<T, content::Array> ||
                           std::same_as<T, content::Object>;

template <serializer_like S>
struct serialize_traits<S, content::Value> {
    using value_type = typename S::value_type;
    using error_type = typename S::error_type;

    static auto serialize(S& s, const content::Value& value)
        -> std::expected<value_type, error_type> {
        return std::visit(
            [&](const auto& stored) -> std::expected<value_type, error_type> {
                using U = std::remove_cvref_t<decltype(stored)>;
                if constexpr(std::same_as<U, std::monostate>) {
                    return s.serialize_null();
                } else if constexpr(std::same_as<U, bool>) {
                    return s.serialize_bool(stored);
                } else if constexpr(std::same_as<U, std::int64_t>) {
                    return s.serialize_int(stored);
                } else if constexpr(std::same_as<U, std::uint64_t>) {
                    return s.serialize_uint(stored);
                } else if constexpr(std::same_as<U, double>) {
                    return s.serialize_float(stored);
                } else if constexpr(std::same_as<U, std::string>) {
                    return s.serialize_str(std::string_view(stored));
                } else if constexpr(std::same_as<U, content::Array>) {
                    return serialize_traits<S, content::Array>::serialize(s, stored);
                } else {
                    return serialize_traits<S, content::Object>::serialize(s, stored);
                }
            },
            value.variant());
    }
};

template <serializer_like S>
struct serialize_traits<S, content::Array> {
    using value_type = typename S::value_type;
    using error_type = typename S::error_type;

    static auto serialize(S& s, const content::Array& value)
        -> std::expected<value_type, error_type> {
        KOTA_EXPECTED_TRY_V(auto seq, s.serialize_seq(value.size()));
        for(const auto& item: value) {
            KOTA_EXPECTED_TRY(seq.serialize_element(item));
        }
        return seq.end();
    }
};

template <serializer_like S>
struct serialize_traits<S, content::Object> {
    using value_type = typename S::value_type;
    using error_type = typename S::error_type;

    static auto serialize(S& s, const content::Object& value)
        -> std::expected<value_type, error_type> {
        KOTA_EXPECTED_TRY_V(auto map, s.serialize_map(value.size()));
        for(const auto& entry: value) {
            KOTA_EXPECTED_TRY(map.serialize_entry(std::string_view(entry.key), entry.value));
        }
        return map.end();
    }
};

// --- content::Serializer: fast-path that moves the Value directly into the stream. ---

template <typename Config>
struct serialize_traits<content::Serializer<Config>, content::Value> {
    using value_type = typename content::Serializer<Config>::value_type;
    using error_type = typename content::Serializer<Config>::error_type;

    static auto serialize(content::Serializer<Config>& s, const content::Value& value)
        -> std::expected<value_type, error_type> {
        return s.append_dom_value(value);
    }
};

template <typename Config>
struct serialize_traits<content::Serializer<Config>, content::Array> {
    using value_type = typename content::Serializer<Config>::value_type;
    using error_type = typename content::Serializer<Config>::error_type;

    static auto serialize(content::Serializer<Config>& s, const content::Array& value)
        -> std::expected<value_type, error_type> {
        return s.append_dom_value(value);
    }
};

template <typename Config>
struct serialize_traits<content::Serializer<Config>, content::Object> {
    using value_type = typename content::Serializer<Config>::value_type;
    using error_type = typename content::Serializer<Config>::error_type;

    static auto serialize(content::Serializer<Config>& s, const content::Object& value)
        -> std::expected<value_type, error_type> {
        return s.append_dom_value(value);
    }
};

}  // namespace kota::codec
