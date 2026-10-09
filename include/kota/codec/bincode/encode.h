#pragma once

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <iterator>
#include <limits>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "kota/support/config.h"
#include "kota/support/expected_try.h"
#include "kota/codec/bincode/type.h"
#include "kota/codec/dyn/document.h"
#include "kota/codec/visit/config.h"
#include "kota/codec/visit/encode.h"

namespace kota::codec::bincode {

/// Streams values onto the end of `buf` in bincode's little-endian layout
/// (see the `# Lowerings` table on bincode::format in type.h). Every value
/// that is a number is widened before writing — ints to int64/uint64, floats
/// to double — so a value's encoded size never depends on its declared
/// width; Reader narrows back with range checks. Containers write only their
/// element count and structs write nothing at all, which is what makes the
/// format non-self-describing.
struct Writer {
    std::vector<std::byte>& buf;
    /// The end of what is written: buf grows ahead of it, its bytes past it
    /// zero, and to_bytes cuts it back to it.
    std::size_t size = buf.size();
    using format = bincode::format;
    constexpr static bool human_readable = false;
    /// Struct fields are concatenated with no marker, so skip_if never omits
    /// one: decode reads every field in order.
    constexpr static bool writes_every_field = true;

    /// Where the next n bytes go.
    std::byte* claim(std::size_t n) {
        if(buf.size() - size < n) [[unlikely]] {
            grow(n);
        }
        auto* at = buf.data() + size;
        size += n;
        return at;
    }

    KOTA_NOINLINE void grow(std::size_t n) {
        buf.resize(std::max({buf.size() * 2, size + n, std::size_t{64}}));
    }

    template <typename T>
        requires std::integral<T>
    void write_le(T value) {
        auto raw = static_cast<std::make_unsigned_t<T>>(value);
        auto* at = claim(sizeof(raw));
        for(std::size_t i = 0; i < sizeof(raw); ++i) {
            at[i] = static_cast<std::byte>(raw >> (i * 8));
        }
    }

    void write_u8(std::uint8_t value) {
        *claim(1) = static_cast<std::byte>(value);
    }

    /// A length or an element count, in as few bytes as detail::LengthMarker
    /// lets it.
    void write_length(std::uint64_t length) {
        using enum detail::LengthMarker;
        if(length < std::to_underlying(U16)) {
            write_u8(static_cast<std::uint8_t>(length));
        } else if(length <= std::numeric_limits<std::uint16_t>::max()) {
            write_u8(std::to_underlying(U16));
            write_le(static_cast<std::uint16_t>(length));
        } else if(length <= std::numeric_limits<std::uint32_t>::max()) {
            write_u8(std::to_underlying(U32));
            write_le(static_cast<std::uint32_t>(length));
        } else {
            write_u8(std::to_underlying(U64));
            write_le(length);
        }
    }

    /// A string or byte sequence: its length, then the bytes.
    void write_blob(std::span<const std::byte> bytes) {
        write_length(bytes.size());
        if(!bytes.empty()) {
            std::memcpy(claim(bytes.size()), bytes.data(), bytes.size());
        }
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
        write_length(std::ranges::size(c));
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
        write_length(std::ranges::size(c));
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
auto to_bytes(const T& value) -> std::expected<std::vector<std::byte>, rich_error> {
    std::vector<std::byte> buf;
    Writer vis{buf};
    KOTA_EXPECTED_TRY(codec::detail::run_encode<Config>(vis, value));
    buf.resize(vis.size);
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

/// A bincode document does not say what a value is, so a dyn::Value writes
/// its ValueKind as one byte before what it holds (see bincode::format).
/// Declared in bincode's own header, which includes the type, so every
/// translation unit that can write bincode sees it rather than dyn's
/// untagged form, whose bytes cannot be read back.
template <typename Config>
struct serialize_visit<bincode::Writer, dyn::Value, Config> {
    static bool visit(bincode::Writer& vis, const dyn::Value& value) {
        vis.write_u8(static_cast<std::uint8_t>(value.kind()));
        return std::visit(
            [&](const auto& stored) -> bool { return encode_value<Config>(vis, stored); },
            value.variant());
    }
};

}  // namespace kota::codec
