#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#include "kota/support/expected_try.h"
#include "kota/codec/bincode/type.h"
#include "kota/codec/visit/config.h"
#include "kota/codec/visit/encode.h"

namespace kota::codec::bincode {

/// Streams values into `buf` in bincode's fixed little-endian layout (see
/// the `# Lowerings` table on bincode::format in type.h). Everything is
/// widened before writing — ints to int64/uint64, floats to double — so a
/// value's encoded size never depends on its declared width; Reader narrows
/// back with range checks. Containers write only a u64 element count and
/// structs write nothing at all, which is what makes the format
/// non-self-describing.
struct Writer {
    std::vector<std::byte>& buf;
    using error_type = rich_error;
    using format = bincode::format;
    constexpr static bool human_readable = false;
    /// Struct fields are concatenated with no marker, so skip_if never omits
    /// one: decode reads every field in order.
    constexpr static bool writes_every_field = true;

    template <typename T>
        requires std::integral<T>
    void write_le(T value) {
        using unsigned_t = std::make_unsigned_t<T>;
        unsigned_t raw = static_cast<unsigned_t>(value);
        for(std::size_t i = 0; i < sizeof(unsigned_t); ++i) {
            auto byte = static_cast<std::uint8_t>((raw >> (i * 8)) & 0xFFU);
            buf.push_back(static_cast<std::byte>(byte));
        }
    }

    void write_u8(std::uint8_t value) {
        buf.push_back(static_cast<std::byte>(value));
    }

    /// A string or byte sequence: u64 length prefix, then the bytes.
    void write_blob(std::span<const std::byte> bytes) {
        write_le(static_cast<std::uint64_t>(bytes.size()));
        buf.insert(buf.end(), bytes.begin(), bytes.end());
    }

    bool visit_bool(bool v) {
        write_u8(v ? 1 : 0);
        return true;
    }

    template <typename T>
    bool visit_int(T v) {
        write_le(static_cast<std::int64_t>(v));
        return true;
    }

    template <typename T>
    bool visit_uint(T v) {
        write_le(static_cast<std::uint64_t>(v));
        return true;
    }

    template <typename T>
    bool visit_float(T v) {
        double d = static_cast<double>(v);
        auto bits = std::bit_cast<std::uint64_t>(d);
        write_le(bits);
        return true;
    }

    template <typename T>
    bool visit_char(T v) {
        write_u8(static_cast<std::uint8_t>(v));
        return true;
    }

    template <typename T>
    bool visit_str(const T& v) {
        std::string_view sv(v);
        write_blob(std::as_bytes(std::span(sv.data(), sv.size())));
        return true;
    }

    template <typename T>
    bool visit_bytes(const T& v) {
        write_blob(std::as_bytes(std::span(std::data(v), std::size(v))));
        return true;
    }

    bool visit_null() {
        write_u8(0x00);
        return true;
    }

    template <typename T, typename Body>
    bool visit_some(const T&, Body&& body) {
        write_u8(0x01);
        return body(*this);
    }

    template <typename T, typename Body>
    bool visit_struct(const T&, Body&& body) {
        return body(*this);
    }

    template <typename F>
    bool visit_field(std::size_t /*index*/, std::string_view /*name*/, F&& field_writer) {
        return field_writer(*this);
    }

    template <typename Container, typename Body>
    bool visit_seq(const Container& c, Body&& body) {
        write_le(static_cast<std::uint64_t>(std::ranges::size(c)));
        return body(*this);
    }

    template <typename F>
    bool visit_element(F&& element_writer) {
        return element_writer(*this);
    }

    template <typename T, typename Body>
    bool visit_tuple(const T&, Body&& body) {
        return body(*this);
    }

    template <typename Container, typename Body>
    bool visit_map(const Container& c, Body&& body) {
        write_le(static_cast<std::uint64_t>(std::ranges::size(c)));
        return body(*this);
    }

    template <typename KF, typename VF>
    bool visit_entry(KF&& key_fn, VF&& value_fn) {
        KOTA_CODEC_TRY(key_fn(*this));
        return value_fn(*this);
    }

    template <typename Body>
    bool visit_variant(std::size_t index, Body&& body) {
        write_le(static_cast<std::uint32_t>(index));
        return body(*this);
    }
};

/// Encodes `value` as a bincode byte buffer. Decode requires the same T and
/// Config — the format carries no self-description.
template <typename Config = void, typename T>
auto to_bytes(const T& value) -> std::expected<std::vector<std::byte>, bincode::error> {
    std::vector<std::byte> buf;
    Writer vis{buf};
    KOTA_EXPECTED_TRY(codec::detail::run_encode<Config>(vis, value));
    return buf;
}

}  // namespace kota::codec::bincode

namespace kota::codec {

// std::monostate has a single value, so unlike other null-like types it
// writes no byte at all, wherever it appears.
template <typename Config>
struct serialize_visit<bincode::Writer, std::monostate, Config> {
    static bool visit(bincode::Writer& /*vis*/, const std::monostate& /*value*/) {
        return true;
    }
};

}  // namespace kota::codec
