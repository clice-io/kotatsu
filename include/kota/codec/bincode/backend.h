#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include "kota/meta/type_kind.h"
#include "kota/codec/bincode/error.h"

namespace kota::codec::bincode {

/// Streaming byte reader state for bincode deserialization.
struct byte_reader {
    std::span<const std::byte> bytes;
    std::size_t offset = 0;

    template <typename T>
        requires std::integral<T>
    error_kind read_integral(T& out) {
        using unsigned_t = std::make_unsigned_t<T>;
        if(offset + sizeof(unsigned_t) > bytes.size())
            return error_kind::unexpected_eof;

        unsigned_t raw = 0;
        for(std::size_t i = 0; i < sizeof(unsigned_t); ++i) {
            auto byte = std::to_integer<std::uint8_t>(bytes[offset + i]);
            raw |= (static_cast<unsigned_t>(byte) << (i * 8));
        }
        offset += sizeof(unsigned_t);

        if constexpr(std::signed_integral<T>) {
            out = std::bit_cast<T>(raw);
        } else {
            out = static_cast<T>(raw);
        }
        return error_kind::ok;
    }

    error_kind read_u8(std::uint8_t& out) {
        return read_integral(out);
    }

    error_kind read_length(std::size_t& out) {
        std::uint64_t raw = 0;
        auto err = read_integral(raw);
        if(err != error_kind::ok)
            return err;
        if(raw > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)()))
            return error_kind::number_out_of_range;
        out = static_cast<std::size_t>(raw);
        return error_kind::ok;
    }
};

struct bincode_backend {
    /// value_type is a pointer to the shared byte_reader state.
    /// All reads advance the reader's offset.
    using value_type = byte_reader*;
    using error_type = error_kind;
    static constexpr error_type success = error_kind::ok;
    static constexpr error_type type_mismatch = error_kind::type_mismatch;
    static constexpr error_type number_out_of_range = error_kind::number_out_of_range;

    /// Marker: this backend uses positional struct layout (no field names).
    static constexpr bool positional = true;

    static error_type read_bool(value_type& v, bool& out) {
        std::uint8_t raw = 0;
        auto err = v->read_u8(raw);
        if(err != success)
            return err;
        if(raw > 1U)
            return type_mismatch;
        out = raw == 1U;
        return success;
    }

    static error_type read_int64(value_type& v, std::int64_t& out) {
        return v->read_integral(out);
    }

    static error_type read_uint64(value_type& v, std::uint64_t& out) {
        return v->read_integral(out);
    }

    static error_type read_double(value_type& v, double& out) {
        std::uint64_t raw = 0;
        auto err = v->read_integral(raw);
        if(err != success)
            return err;
        out = std::bit_cast<double>(raw);
        return success;
    }

    static error_type read_string(value_type& v, std::string_view& out) {
        std::size_t length = 0;
        auto err = v->read_length(length);
        if(err != success)
            return err;
        if(v->offset + length > v->bytes.size())
            return error_kind::unexpected_eof;
        out = std::string_view(
            reinterpret_cast<const char*>(v->bytes.data() + v->offset), length);
        v->offset += length;
        return success;
    }

    static error_type read_is_null(value_type& v, bool& is_null) {
        std::uint8_t tag = 0;
        auto err = v->read_u8(tag);
        if(err != success)
            return err;
        if(tag == 0U) {
            is_null = true;
            return success;
        }
        if(tag == 1U) {
            is_null = false;
            return success;
        }
        return type_mismatch;
    }

    /// Bincode does not have objects. This is unsupported.
    template <typename Visitor>
    static error_type visit_object(value_type&, Visitor&&) {
        return error_kind::unsupported_operation;
    }

    /// Bincode arrays: length-prefixed, elements read sequentially.
    template <typename Visitor>
    static error_type visit_array(value_type& src, Visitor&& vis) {
        std::size_t length = 0;
        auto err = src->read_length(length);
        if(err != success)
            return err;
        for(std::size_t i = 0; i < length; ++i) {
            err = vis.visit_element(src);
            if(err != success) [[unlikely]]
                return err;
        }
        return success;
    }

    /// Positional struct: read fields in schema order.
    /// The visitor's visit_field_positional(index, val) is called for each slot.
    template <typename Visitor>
    static error_type visit_struct_positional(value_type& src, Visitor&& vis) {
        return vis.visit_all_positional(src);
    }

    /// Bincode variant: read a uint32 index, then deserialize the selected alternative.
    static error_type read_variant_index(value_type& v, std::uint32_t& out) {
        return v->read_integral(out);
    }

    /// Bincode does not have type introspection.
    static meta::type_kind kind_of(value_type&) {
        return meta::type_kind::unknown;
    }

    /// Bincode bytes: length-prefixed raw bytes.
    static error_type read_bytes(value_type& v, std::vector<std::byte>& out) {
        std::size_t length = 0;
        auto err = v->read_length(length);
        if(err != success)
            return err;
        if(v->offset + length > v->bytes.size())
            return error_kind::unexpected_eof;
        if(length == 0) {
            out.clear();
            return success;
        }
        out.assign(v->bytes.begin() + static_cast<std::ptrdiff_t>(v->offset),
                   v->bytes.begin() + static_cast<std::ptrdiff_t>(v->offset + length));
        v->offset += length;
        return success;
    }
};

template <typename Config>
struct bincode_backend_with_config : bincode_backend {
    using config_type = Config;
    using base_backend_type = bincode_backend;
};

}  // namespace kota::codec::bincode
