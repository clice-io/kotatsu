#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

#include "kota/support/expected_try.h"
#include "kota/codec/arena/decode.h"
#include "kota/codec/arena/traits.h"
#include "kota/codec/config.h"
#include "kota/codec/flatcode/serializer.h"

namespace kota::codec::flatcode {

namespace detail {

// Little-endian scalar read with memcpy (bypasses alignment constraints).
template <typename T>
inline auto read_scalar(const std::uint8_t* p) -> T {
    T value{};
    std::memcpy(&value, p, sizeof(T));
    return value;
}

// === Lightweight view wrappers used by the arena decode layer =============

template <typename T>
class scalar_vector_view {
public:
    scalar_vector_view() = default;

    scalar_vector_view(const std::uint8_t* elements_ptr, std::size_t count) :
        elements(elements_ptr), count_(count) {}

    auto size() const -> std::size_t {
        return count_;
    }

    auto operator[](std::size_t index) const -> T {
        return read_scalar<T>(elements + index * sizeof(T));
    }

private:
    const std::uint8_t* elements = nullptr;
    std::size_t count_ = 0;
};

class string_vector_view {
public:
    string_vector_view() = default;

    string_vector_view(const std::uint8_t* buffer_base,
                       std::size_t buffer_size,
                       const std::uint8_t* offsets_ptr,
                       std::size_t count) :
        base(buffer_base), base_size(buffer_size), offsets(offsets_ptr), count_(count) {}

    auto size() const -> std::size_t {
        return count_;
    }

    auto operator[](std::size_t index) const -> std::string_view {
        const auto off = read_scalar<std::uint32_t>(offsets + index * 4);
        if(off == 0 || off + 4 > base_size) {
            return {};
        }
        const auto len = read_scalar<std::uint32_t>(base + off);
        if(off + 4 + len > base_size) {
            return {};
        }
        return std::string_view(reinterpret_cast<const char*>(base + off + 4), len);
    }

private:
    const std::uint8_t* base = nullptr;
    std::size_t base_size = 0;
    const std::uint8_t* offsets = nullptr;
    std::size_t count_ = 0;
};

template <typename T>
class inline_struct_vector_view {
public:
    inline_struct_vector_view() = default;

    inline_struct_vector_view(const std::uint8_t* elements_ptr, std::size_t count) :
        elements(elements_ptr), count_(count) {}

    auto size() const -> std::size_t {
        return count_;
    }

    auto operator[](std::size_t index) const -> T {
        T value{};
        std::memcpy(&value, elements + index * sizeof(T), sizeof(T));
        return value;
    }

private:
    const std::uint8_t* elements = nullptr;
    std::size_t count_ = 0;
};

template <typename TableView>
class table_vector_view {
public:
    table_vector_view() = default;

    table_vector_view(const std::uint8_t* buffer_base,
                      std::size_t buffer_size,
                      const std::uint8_t* offsets_ptr,
                      std::size_t count) :
        base(buffer_base), base_size(buffer_size), offsets(offsets_ptr), count_(count) {}

    auto size() const -> std::size_t {
        return count_;
    }

    auto operator[](std::size_t index) const -> TableView {
        const auto off = read_scalar<std::uint32_t>(offsets + index * 4);
        if(off == 0 || off >= base_size) {
            return TableView{};
        }
        return TableView(base, base_size, off);
    }

private:
    const std::uint8_t* base = nullptr;
    std::size_t base_size = 0;
    const std::uint8_t* offsets = nullptr;
    std::size_t count_ = 0;
};

}  // namespace detail

// Arena-codec backend for the flatcode wire format (deserialize side).
template <typename Config = config::default_config>
class Deserializer {
public:
    using config_type = Config;
    using error_type = object_error_code;
    using slot_id = std::uint32_t;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    template <typename T>
    constexpr static bool can_inline_struct = detail::is_inline_struct_v<T>;

    // Slot-id helpers (mirror of the serializer).
    static auto field_slot_id(std::size_t index) -> result_t<slot_id> {
        if(index >= detail::max_slot_count) {
            return std::unexpected(object_error_code::too_many_fields);
        }
        return static_cast<slot_id>(index);
    }

    static auto variant_tag_slot_id() -> slot_id {
        return 0;
    }

    static auto variant_payload_slot_id(std::size_t index) -> result_t<slot_id> {
        return field_slot_id(index + 1);
    }

    // === Table view ========================================================

    class TableView {
    public:
        TableView() = default;

        TableView(const std::uint8_t* buffer_base,
                  std::size_t buffer_size,
                  std::uint32_t table_offset) :
            base(buffer_base), base_size(buffer_size), table_off(table_offset) {
            if(base == nullptr || table_off == 0 || table_off + 4 > base_size) {
                base = nullptr;
                base_size = 0;
                table_off = 0;
                n_slots_cached = 0;
                return;
            }
            n_slots_cached = detail::read_scalar<std::uint32_t>(base + table_off);
            // Header must fit in buffer: 4 + 4*n_slots bytes after table_off.
            const auto header_bytes = 4U + 4U * n_slots_cached;
            if(static_cast<std::size_t>(table_off) + header_bytes > base_size) {
                base = nullptr;
                base_size = 0;
                table_off = 0;
                n_slots_cached = 0;
            }
        }

        auto valid() const -> bool {
            return base != nullptr;
        }

        auto has(slot_id sid) const -> bool {
            if(!valid() || sid >= n_slots_cached) {
                return false;
            }
            return slot_offset(sid) != 0;
        }

        auto any_field_present() const -> bool {
            if(!valid()) {
                return false;
            }
            for(std::uint32_t i = 0; i < n_slots_cached; ++i) {
                if(slot_offset(i) != 0) {
                    return true;
                }
            }
            return false;
        }

        template <typename T>
        auto get_scalar(slot_id sid) const -> T {
            if(!has(sid)) {
                return T{};
            }
            const auto off = slot_offset(sid);
            if(static_cast<std::size_t>(table_off) + off + sizeof(T) > base_size) {
                return T{};
            }
            return detail::read_scalar<T>(base + table_off + off);
        }

        // Absolute offset (from buffer start) at which this slot's data lives.
        // Returns 0 if slot is absent.
        auto slot_absolute_offset(slot_id sid) const -> std::uint32_t {
            if(!has(sid)) {
                return 0;
            }
            return table_off + slot_offset(sid);
        }

        auto buffer_base() const -> const std::uint8_t* {
            return base;
        }

        auto buffer_size() const -> std::size_t {
            return base_size;
        }

        auto table_start() const -> std::uint32_t {
            return table_off;
        }

    private:
        auto slot_offset(std::uint32_t sid) const -> std::uint32_t {
            return detail::read_scalar<std::uint32_t>(base + table_off + 4 + sid * 4);
        }

        const std::uint8_t* base = nullptr;
        std::size_t base_size = 0;
        std::uint32_t table_off = 0;
        std::uint32_t n_slots_cached = 0;
    };

    // === Construction ======================================================

    explicit Deserializer(std::span<const std::uint8_t> bytes) {
        initialize(bytes);
    }

    explicit Deserializer(std::span<const std::byte> bytes) {
        if(bytes.empty()) {
            set_invalid(object_error_code::invalid_state);
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
        return is_valid ? object_error_code::none : last_error;
    }

    // === Arena decode accessors ============================================

    auto root_view() const -> TableView {
        if(!is_valid) {
            return TableView{};
        }
        return TableView(buffer.data(), buffer.size(), root_offset);
    }

    auto get_string(TableView view, slot_id sid) const -> result_t<std::string_view> {
        if(!view.valid() || !view.has(sid)) {
            return std::unexpected(object_error_code::invalid_state);
        }
        const auto off =
            detail::read_scalar<std::uint32_t>(buffer.data() + view.slot_absolute_offset(sid));
        if(off == 0 || off + 4 > buffer.size()) {
            return std::unexpected(object_error_code::invalid_state);
        }
        const auto len = detail::read_scalar<std::uint32_t>(buffer.data() + off);
        if(off + 4 + len > buffer.size()) {
            return std::unexpected(object_error_code::invalid_state);
        }
        return std::string_view(reinterpret_cast<const char*>(buffer.data() + off + 4), len);
    }

    auto get_bytes(TableView view, slot_id sid) const -> result_t<std::span<const std::byte>> {
        if(!view.valid() || !view.has(sid)) {
            return std::unexpected(object_error_code::invalid_state);
        }
        const auto off =
            detail::read_scalar<std::uint32_t>(buffer.data() + view.slot_absolute_offset(sid));
        if(off == 0 || off + 4 > buffer.size()) {
            return std::unexpected(object_error_code::invalid_state);
        }
        const auto len = detail::read_scalar<std::uint32_t>(buffer.data() + off);
        if(off + 4 + len > buffer.size()) {
            return std::unexpected(object_error_code::invalid_state);
        }
        return std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(buffer.data() + off + 4),
            len);
    }

    template <typename T>
    auto get_scalar_vector(TableView view, slot_id sid) const
        -> result_t<detail::scalar_vector_view<T>> {
        auto data = resolve_vector<T>(view, sid);
        if(!data) {
            return std::unexpected(data.error());
        }
        return detail::scalar_vector_view<T>(data->first, data->second);
    }

    auto get_string_vector(TableView view, slot_id sid) const
        -> result_t<detail::string_vector_view> {
        if(!view.valid() || !view.has(sid)) {
            return std::unexpected(object_error_code::invalid_state);
        }
        const auto vec_off =
            detail::read_scalar<std::uint32_t>(buffer.data() + view.slot_absolute_offset(sid));
        if(vec_off == 0 || vec_off + 4 > buffer.size()) {
            return std::unexpected(object_error_code::invalid_state);
        }
        const auto count = detail::read_scalar<std::uint32_t>(buffer.data() + vec_off);
        const auto offsets_off = vec_off + 4U;
        if(offsets_off + 4U * count > buffer.size()) {
            return std::unexpected(object_error_code::invalid_state);
        }
        return detail::string_vector_view(buffer.data(),
                                          buffer.size(),
                                          buffer.data() + offsets_off,
                                          count);
    }

    template <typename T>
    auto get_inline_struct(TableView view, slot_id sid) const -> result_t<T> {
        if(!view.valid() || !view.has(sid)) {
            return std::unexpected(object_error_code::invalid_state);
        }
        const auto off = view.slot_absolute_offset(sid);
        if(static_cast<std::size_t>(off) + sizeof(T) > buffer.size()) {
            return std::unexpected(object_error_code::invalid_state);
        }
        T value{};
        std::memcpy(&value, buffer.data() + off, sizeof(T));
        return value;
    }

    template <typename T>
    auto get_inline_struct_vector(TableView view, slot_id sid) const
        -> result_t<detail::inline_struct_vector_view<T>> {
        auto data = resolve_vector<T>(view, sid);
        if(!data) {
            return std::unexpected(data.error());
        }
        return detail::inline_struct_vector_view<T>(data->first, data->second);
    }

    auto get_table(TableView view, slot_id sid) const -> result_t<TableView> {
        if(!view.valid() || !view.has(sid)) {
            return std::unexpected(object_error_code::invalid_state);
        }
        const auto nested_off =
            detail::read_scalar<std::uint32_t>(buffer.data() + view.slot_absolute_offset(sid));
        if(nested_off == 0 || nested_off >= buffer.size()) {
            return std::unexpected(object_error_code::invalid_state);
        }
        TableView nested(buffer.data(), buffer.size(), nested_off);
        if(!nested.valid()) {
            return std::unexpected(object_error_code::invalid_state);
        }
        return nested;
    }

    auto get_table_vector(TableView view, slot_id sid) const
        -> result_t<detail::table_vector_view<TableView>> {
        if(!view.valid() || !view.has(sid)) {
            return std::unexpected(object_error_code::invalid_state);
        }
        const auto vec_off =
            detail::read_scalar<std::uint32_t>(buffer.data() + view.slot_absolute_offset(sid));
        if(vec_off == 0 || vec_off + 4 > buffer.size()) {
            return std::unexpected(object_error_code::invalid_state);
        }
        const auto count = detail::read_scalar<std::uint32_t>(buffer.data() + vec_off);
        const auto offsets_off = vec_off + 4U;
        if(offsets_off + 4U * count > buffer.size()) {
            return std::unexpected(object_error_code::invalid_state);
        }
        return detail::table_vector_view<TableView>(buffer.data(),
                                                    buffer.size(),
                                                    buffer.data() + offsets_off,
                                                    count);
    }

    // === Top-level entry ===================================================

    template <typename T>
    auto deserialize(T& value) const -> result_t<void> {
        if(!is_valid) {
            return std::unexpected(last_error);
        }
        return arena::decode_root<Config>(*this, value);
    }

private:
    // Resolve a vector slot to (elements_ptr, count). For scalar and
    // inline-struct vectors, elements begin at `vec_off + 4 + pad_to(alignof(T))`.
    template <typename T>
    auto resolve_vector(TableView view, slot_id sid) const
        -> result_t<std::pair<const std::uint8_t*, std::size_t>> {
        if(!view.valid() || !view.has(sid)) {
            return std::unexpected(object_error_code::invalid_state);
        }
        const auto vec_off =
            detail::read_scalar<std::uint32_t>(buffer.data() + view.slot_absolute_offset(sid));
        if(vec_off == 0 || vec_off + 4 > buffer.size()) {
            return std::unexpected(object_error_code::invalid_state);
        }
        const auto count = detail::read_scalar<std::uint32_t>(buffer.data() + vec_off);
        std::size_t elements_off = vec_off + 4U;
        constexpr std::size_t a = alignof(T);
        if constexpr(a > 1) {
            while(elements_off % a != 0U) {
                ++elements_off;
            }
        }
        if(elements_off + count * sizeof(T) > buffer.size()) {
            return std::unexpected(object_error_code::invalid_state);
        }
        return std::make_pair(buffer.data() + elements_off, static_cast<std::size_t>(count));
    }

    auto initialize(std::span<const std::uint8_t> bytes) -> void {
        if(bytes.size() < detail::header_size) {
            set_invalid(object_error_code::invalid_state);
            return;
        }
        const auto magic = detail::read_scalar<std::uint32_t>(bytes.data());
        if(magic != detail::magic_evfc) {
            set_invalid(object_error_code::invalid_state);
            return;
        }
        const auto root_off = detail::read_scalar<std::uint32_t>(bytes.data() + 4);
        if(root_off == 0 || root_off >= bytes.size()) {
            set_invalid(object_error_code::invalid_state);
            return;
        }

        buffer.assign(bytes.begin(), bytes.end());
        root_offset = root_off;

        // Validate root table header fits.
        TableView root(buffer.data(), buffer.size(), root_offset);
        if(!root.valid()) {
            set_invalid(object_error_code::invalid_state);
            return;
        }

        is_valid = true;
        last_error = object_error_code::none;
    }

    auto set_invalid(error_type error) -> void {
        is_valid = false;
        last_error = error;
        buffer.clear();
        root_offset = 0;
    }

    std::vector<std::uint8_t> buffer;
    std::uint32_t root_offset = 0;
    bool is_valid = false;
    error_type last_error = object_error_code::invalid_state;
};

template <typename Config = config::default_config, typename T>
auto from_flatcode(std::span<const std::uint8_t> bytes, T& value) -> object_result_t<void> {
    Deserializer<Config> deserializer(bytes);
    if(!deserializer.valid()) {
        return std::unexpected(deserializer.error());
    }
    KOTA_EXPECTED_TRY(deserializer.deserialize(value));
    return {};
}

template <typename Config = config::default_config, typename T>
auto from_flatcode(std::span<const std::byte> bytes, T& value) -> object_result_t<void> {
    Deserializer<Config> deserializer(bytes);
    if(!deserializer.valid()) {
        return std::unexpected(deserializer.error());
    }
    KOTA_EXPECTED_TRY(deserializer.deserialize(value));
    return {};
}

template <typename Config = config::default_config, typename T>
auto from_flatcode(const std::vector<std::uint8_t>& bytes, T& value) -> object_result_t<void> {
    return from_flatcode<Config>(std::span<const std::uint8_t>(bytes.data(), bytes.size()), value);
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_flatcode(std::span<const std::uint8_t> bytes) -> object_result_t<T> {
    T value{};
    KOTA_EXPECTED_TRY(from_flatcode<Config>(bytes, value));
    return value;
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_flatcode(std::span<const std::byte> bytes) -> object_result_t<T> {
    T value{};
    KOTA_EXPECTED_TRY(from_flatcode<Config>(bytes, value));
    return value;
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_flatcode(const std::vector<std::uint8_t>& bytes) -> object_result_t<T> {
    return from_flatcode<T, Config>(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
}

}  // namespace kota::codec::flatcode
