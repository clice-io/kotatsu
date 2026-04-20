#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "kota/support/expected_try.h"
#include "kota/codec/detail/arena_decode.h"
#include "kota/codec/detail/arena_traits.h"
#include "kota/codec/config.h"
#include "kota/codec/flatbuffers/serializer.h"
#include "kota/codec/flatbuffers/struct_layout.h"

#if __has_include(<flatbuffers/flatbuffers.h>)
#include "flatbuffers/flatbuffers.h"
#else
#error                                                                                             \
    "flatbuffers/flatbuffers.h not found. Enable KOTA_CODEC_ENABLE_FLATBUFFERS or add flatbuffers include paths."
#endif

namespace kota::codec::flatbuffers {

namespace detail {

// === Small typed view wrappers used by the arena decode layer =============

template <typename T>
class scalar_vector_view {
public:
    using underlying = ::flatbuffers::Vector<T>;

    scalar_vector_view() = default;

    explicit scalar_vector_view(const underlying* v) : vector(v) {}

    auto size() const -> std::size_t {
        return vector == nullptr ? 0 : vector->size();
    }

    auto operator[](std::size_t index) const -> T {
        return vector->Get(static_cast<::flatbuffers::uoffset_t>(index));
    }

private:
    const underlying* vector = nullptr;
};

class string_vector_view {
public:
    using underlying = ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>>;

    string_vector_view() = default;

    explicit string_vector_view(const underlying* v) : vector(v) {}

    auto size() const -> std::size_t {
        return vector == nullptr ? 0 : vector->size();
    }

    auto operator[](std::size_t index) const -> std::string_view {
        const auto* text = vector->GetAsString(static_cast<::flatbuffers::uoffset_t>(index));
        if(text == nullptr) {
            return {};
        }
        return std::string_view(text->data(), text->size());
    }

private:
    const underlying* vector = nullptr;
};

template <typename T>
class inline_struct_vector_view {
public:
    using underlying = ::flatbuffers::Vector<const T*>;

    inline_struct_vector_view() = default;

    explicit inline_struct_vector_view(const underlying* v) : vector(v) {}

    auto size() const -> std::size_t {
        return vector == nullptr ? 0 : vector->size();
    }

    auto operator[](std::size_t index) const -> const T& {
        return *vector->Get(static_cast<::flatbuffers::uoffset_t>(index));
    }

private:
    const underlying* vector = nullptr;
};

template <typename TableView>
class table_vector_view {
public:
    using underlying = ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::Table>>;

    table_vector_view() = default;

    explicit table_vector_view(const underlying* v) : vector(v) {}

    auto size() const -> std::size_t {
        return vector == nullptr ? 0 : vector->size();
    }

    auto operator[](std::size_t index) const -> TableView {
        const auto* nested =
            vector->GetAs<::flatbuffers::Table>(static_cast<::flatbuffers::uoffset_t>(index));
        return TableView(nested);
    }

private:
    const underlying* vector = nullptr;
};

}  // namespace detail

// Arena-codec backend for flatbuffers (deserialization side).
//
// Wraps a ::flatbuffers::Table* and exposes the arena deserializer trait.
// All type-dispatch logic lives in kota::codec::arena::decode_* — this class
// is a thin adapter that turns arena accessors into FlatBuffers table reads.
template <typename Config = config::default_config>
class Deserializer {
public:
    using config_type = Config;
    using error_type = object_error_code;
    using slot_id = ::flatbuffers::voffset_t;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    // Mirror of the serializer-side predicates; both resolve identically.
    template <typename T>
    constexpr static bool can_inline_struct_field = flatbuffers::can_inline_struct_v<T>;

    template <typename T>
    constexpr static bool can_inline_struct_element = flatbuffers::can_inline_struct_v<T>;

    // Slot-id helpers (mirror of the serializer side).
    static auto field_slot_id(std::size_t index) -> result_t<slot_id> {
        return ::kota::codec::flatbuffers::detail::field_voffset(index);
    }

    static auto variant_tag_slot_id() -> slot_id {
        return ::kota::codec::flatbuffers::detail::first_field;
    }

    static auto variant_payload_slot_id(std::size_t index) -> result_t<slot_id> {
        return ::kota::codec::flatbuffers::detail::variant_payload_voffset(index);
    }

    // === Table view =========================================================

    class TableView {
    public:
        TableView() = default;

        explicit TableView(const ::flatbuffers::Table* t) : table(t) {}

        auto valid() const -> bool {
            return table != nullptr;
        }

        auto has(slot_id sid) const -> bool {
            return table != nullptr && table->GetOptionalFieldOffset(sid) != 0;
        }

        auto any_field_present() const -> bool {
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

        template <typename T>
        auto get_scalar(slot_id sid) const -> T {
            return table->GetField<T>(sid, T{});
        }

        auto raw() const -> const ::flatbuffers::Table* {
            return table;
        }

    private:
        const ::flatbuffers::Table* table = nullptr;
    };

    // === Construction =======================================================

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

    // === Arena decode accessors ============================================

    auto root_view() const -> TableView {
        return TableView(root);
    }

    auto get_string(TableView view, slot_id sid) const -> result_t<std::string_view> {
        if(!view.valid()) {
            return std::unexpected(object_error_code::invalid_state);
        }
        const auto* text = view.raw()->template GetPointer<const ::flatbuffers::String*>(sid);
        if(text == nullptr) {
            return std::unexpected(object_error_code::invalid_state);
        }
        return std::string_view(text->data(), text->size());
    }

    auto get_bytes(TableView view, slot_id sid) const -> result_t<std::span<const std::byte>> {
        if(!view.valid()) {
            return std::unexpected(object_error_code::invalid_state);
        }
        const auto* vector =
            view.raw()->template GetPointer<const ::flatbuffers::Vector<std::uint8_t>*>(sid);
        if(vector == nullptr) {
            return std::unexpected(object_error_code::invalid_state);
        }
        return std::span<const std::byte>(reinterpret_cast<const std::byte*>(vector->data()),
                                          vector->size());
    }

    template <typename T>
    auto get_scalar_vector(TableView view, slot_id sid) const
        -> result_t<detail::scalar_vector_view<T>> {
        if(!view.valid()) {
            return std::unexpected(object_error_code::invalid_state);
        }
        const auto* vector = view.raw()->template GetPointer<const ::flatbuffers::Vector<T>*>(sid);
        if(vector == nullptr) {
            return std::unexpected(object_error_code::invalid_state);
        }
        return detail::scalar_vector_view<T>(vector);
    }

    auto get_string_vector(TableView view, slot_id sid) const
        -> result_t<detail::string_vector_view> {
        if(!view.valid()) {
            return std::unexpected(object_error_code::invalid_state);
        }
        const auto* vector =
            view.raw()
                ->template GetPointer<
                    const ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>>*>(
                    sid);
        if(vector == nullptr) {
            return std::unexpected(object_error_code::invalid_state);
        }
        return detail::string_vector_view(vector);
    }

    template <typename T>
    auto get_inline_struct(TableView view, slot_id sid) const -> result_t<T> {
        if(!view.valid()) {
            return std::unexpected(object_error_code::invalid_state);
        }
        const auto* value = view.raw()->template GetStruct<const T*>(sid);
        if(value == nullptr) {
            return std::unexpected(object_error_code::invalid_state);
        }
        return *value;
    }

    template <typename T>
    auto get_inline_struct_vector(TableView view, slot_id sid) const
        -> result_t<detail::inline_struct_vector_view<T>> {
        if(!view.valid()) {
            return std::unexpected(object_error_code::invalid_state);
        }
        const auto* vector =
            view.raw()->template GetPointer<const ::flatbuffers::Vector<const T*>*>(sid);
        if(vector == nullptr) {
            return std::unexpected(object_error_code::invalid_state);
        }
        return detail::inline_struct_vector_view<T>(vector);
    }

    auto get_table(TableView view, slot_id sid) const -> result_t<TableView> {
        if(!view.valid()) {
            return std::unexpected(object_error_code::invalid_state);
        }
        const auto* nested = view.raw()->template GetPointer<const ::flatbuffers::Table*>(sid);
        if(nested == nullptr) {
            return std::unexpected(object_error_code::invalid_state);
        }
        return TableView(nested);
    }

    auto get_table_vector(TableView view, slot_id sid) const
        -> result_t<detail::table_vector_view<TableView>> {
        if(!view.valid()) {
            return std::unexpected(object_error_code::invalid_state);
        }
        const auto* vector =
            view.raw()
                ->template GetPointer<
                    const ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::Table>>*>(sid);
        if(vector == nullptr) {
            return std::unexpected(object_error_code::invalid_state);
        }
        return detail::table_vector_view<TableView>(vector);
    }

    // === Top-level entry (public) ==========================================

    template <typename T>
    auto deserialize(T& value) const -> result_t<void> {
        if(!is_valid || root == nullptr) {
            return std::unexpected(last_error);
        }
        return arena::decode_root<Config>(*this, value);
    }

private:
    auto initialize(std::span<const std::uint8_t> bytes) -> void {
        if(bytes.empty()) {
            set_invalid(object_error_code::invalid_state);
            return;
        }
        if(!::flatbuffers::BufferHasIdentifier(
               bytes.data(),
               ::kota::codec::flatbuffers::detail::buffer_identifier)) {
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

    KOTA_EXPECTED_TRY(deserializer.deserialize(value));
    return {};
}

template <typename Config = config::default_config, typename T>
auto from_flatbuffer(std::span<const std::byte> bytes, T& value)
    -> std::expected<void, object_error_code> {
    Deserializer<Config> deserializer(bytes);
    if(!deserializer.valid()) {
        return std::unexpected(deserializer.error());
    }

    KOTA_EXPECTED_TRY(deserializer.deserialize(value));
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
    KOTA_EXPECTED_TRY(from_flatbuffer<Config>(bytes, value));
    return value;
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_flatbuffer(std::span<const std::byte> bytes) -> std::expected<T, object_error_code> {
    T value{};
    KOTA_EXPECTED_TRY(from_flatbuffer<Config>(bytes, value));
    return value;
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_flatbuffer(const std::vector<std::uint8_t>& bytes)
    -> std::expected<T, object_error_code> {
    return from_flatbuffer<T, Config>(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
}

}  // namespace kota::codec::flatbuffers
