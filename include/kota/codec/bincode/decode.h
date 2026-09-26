#pragma once

#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <type_traits>

#include "kota/support/numeric.h"
#include "kota/codec/bincode/type.h"
#include "kota/codec/visit/config.h"
#include "kota/codec/visit/context.h"
#include "kota/codec/visit/decode.h"

namespace kota::codec::bincode {

struct Reader;

struct SeqAccess {
    Reader& r;
    std::uint64_t count;
    std::uint64_t idx = 0;

    bool has_element();

    template <typename F>
    bool visit_element(F&& f);
};

struct MapAccess {
    Reader& r;
    std::uint64_t count;
    std::uint64_t idx = 0;

    bool has_entry();

    template <typename KF, typename VF>
    bool visit_entry(KF&& key_reader, VF&& value_reader);
};

/// Mirror of Writer: consumes `data` front to back in bincode's fixed
/// little-endian layout, failing with UnexpectedEof / TypeMismatch /
/// NumberOutOfRange through the scoped error context. Every read is
/// length-checked via check_remaining before touching the buffer; integers
/// are read at their widened 8-byte size and narrowed back into the target
/// type with a range check. from_bytes additionally rejects buffers with
/// bytes left over after the root value (TrailingBytes).
struct Reader {
    std::span<const std::byte> data;
    std::size_t pos = 0;
    using error_type = rich_error;
    using format = bincode::format;
    constexpr static bool human_readable = false;

    bool check_remaining(std::uint64_t n) {
        assert(pos <= data.size());
        if(n > data.size() - pos) {
            return scoped_context<rich_error>::fail(
                rich_error(std::string(error_message(error_kind::UnexpectedEof))));
        }
        return true;
    }

    uint8_t read_u8() {
        auto byte = std::to_integer<uint8_t>(data[pos]);
        ++pos;
        return byte;
    }

    template <typename T>
        requires std::integral<T>
    T read_le() {
        using unsigned_t = std::make_unsigned_t<T>;
        unsigned_t raw = 0;
        for(std::size_t i = 0; i < sizeof(unsigned_t); ++i) {
            auto byte = std::to_integer<uint8_t>(data[pos + i]);
            raw |= (static_cast<unsigned_t>(byte) << (i * 8));
        }
        pos += sizeof(unsigned_t);
        if constexpr(std::signed_integral<T>) {
            return std::bit_cast<T>(raw);
        } else {
            return static_cast<T>(raw);
        }
    }

    bool visit_bool(bool& out) {
        KOTA_CODEC_TRY(check_remaining(1));
        auto byte = read_u8();
        if(byte > 1U) {
            return scoped_context<rich_error>::fail(
                rich_error(std::string(error_message(error_kind::TypeMismatch))));
        }
        out = byte == 1U;
        return true;
    }

    template <typename T>
    bool visit_int(T& out) {
        KOTA_CODEC_TRY(check_remaining(sizeof(std::int64_t)));
        auto raw = read_le<std::int64_t>();
        if(!kota::narrow_int(raw, out)) {
            return scoped_context<rich_error>::fail(
                rich_error(std::string(error_message(error_kind::NumberOutOfRange))));
        }
        return true;
    }

    template <typename T>
    bool visit_uint(T& out) {
        KOTA_CODEC_TRY(check_remaining(sizeof(std::uint64_t)));
        auto raw = read_le<std::uint64_t>();
        if(!kota::narrow_int(raw, out)) {
            return scoped_context<rich_error>::fail(
                rich_error(std::string(error_message(error_kind::NumberOutOfRange))));
        }
        return true;
    }

    template <typename T>
    bool visit_float(T& out) {
        KOTA_CODEC_TRY(check_remaining(sizeof(std::uint64_t)));
        auto raw = read_le<std::uint64_t>();
        double d = std::bit_cast<double>(raw);
        out = static_cast<T>(d);
        return true;
    }

    template <typename T>
    bool visit_str(T& out) {
        KOTA_CODEC_TRY(check_remaining(sizeof(std::uint64_t)));
        auto length = read_le<std::uint64_t>();
        KOTA_CODEC_TRY(check_remaining(length));
        auto len = static_cast<std::size_t>(length);
        const auto* begin = reinterpret_cast<const char*>(data.data() + pos);
        out = T(begin, begin + len);
        pos += len;
        return true;
    }

    template <typename T>
    bool visit_char(T& out) {
        KOTA_CODEC_TRY(check_remaining(1));
        out = static_cast<T>(read_u8());
        return true;
    }

    template <typename T>
    bool visit_bytes(T& out) {
        KOTA_CODEC_TRY(check_remaining(sizeof(std::uint64_t)));
        auto length = read_le<std::uint64_t>();
        KOTA_CODEC_TRY(check_remaining(length));
        auto len = static_cast<std::size_t>(length);
        using value_type = typename T::value_type;
        auto* begin = reinterpret_cast<const value_type*>(data.data() + pos);
        out = T(begin, begin + len);
        pos += len;
        return true;
    }

    template <typename T, typename Body>
    bool visit_option(T& out, Body&& body) {
        KOTA_CODEC_TRY(check_remaining(1));
        auto byte = read_u8();
        if(byte == 0x00) {
            out = T{};
            return true;
        }
        if(byte != 0x01) {
            return scoped_context<rich_error>::fail(
                rich_error(std::string(error_message(error_kind::TypeMismatch))));
        }
        return body(*this);
    }

    bool visit_null() {
        KOTA_CODEC_TRY(check_remaining(1));
        if(read_u8() != 0x00) {
            return scoped_context<rich_error>::fail(
                rich_error(std::string(error_message(error_kind::TypeMismatch))));
        }
        return true;
    }

    template <typename T, typename Body>
    bool visit_struct(T&, Body&& body) {
        return body(*this);
    }

    template <typename Idx, typename F>
    bool visit_field(Idx, std::string_view, F&& r) {
        return r(*this);
    }

    template <typename T, typename Body>
    bool visit_seq(T&, Body&& body) {
        KOTA_CODEC_TRY(check_remaining(sizeof(std::uint64_t)));
        auto count = read_le<std::uint64_t>();
        SeqAccess ctx{*this, count};
        return body(ctx);
    }

    template <typename F>
    bool visit_element(F&& w) {
        return w(*this);
    }

    template <typename T, typename Body>
    bool visit_tuple(T&, Body&& body) {
        return body(*this);
    }

    template <typename T, typename Body>
    bool visit_map(T&, Body&& body) {
        KOTA_CODEC_TRY(check_remaining(sizeof(std::uint64_t)));
        auto count = read_le<std::uint64_t>();
        MapAccess ctx{*this, count};
        return body(ctx);
    }

    template <typename KF, typename VF>
    bool visit_entry(KF&& key_reader, VF&& value_reader) {
        KOTA_CODEC_TRY(key_reader(*this));
        return value_reader(*this);
    }

    template <typename Body>
    bool visit_variant(Body&& body) {
        KOTA_CODEC_TRY(check_remaining(sizeof(std::uint32_t)));
        auto index = read_le<std::uint32_t>();
        return body(static_cast<std::size_t>(index), *this);
    }
};

inline bool SeqAccess::has_element() {
    return idx < count;
}

template <typename F>
bool SeqAccess::visit_element(F&& f) {
    KOTA_CODEC_TRY(f(r));
    ++idx;
    return true;
}

inline bool MapAccess::has_entry() {
    return idx < count;
}

template <typename KF, typename VF>
bool MapAccess::visit_entry(KF&& key_reader, VF&& value_reader) {
    KOTA_CODEC_TRY(key_reader(r));
    KOTA_CODEC_TRY(value_reader(r));
    ++idx;
    return true;
}

/// Decodes a bincode buffer produced by to_bytes with the same T and Config.
/// The whole buffer must be consumed; leftover bytes are an error.
/// Overloads: std::byte / uint8_t spans, into an out-param or returning T.
template <typename Config = void, typename T>
auto from_bytes(std::span<const std::byte> data, T& out) -> std::expected<void, bincode::error> {
    rich_error err;
    scoped_context<rich_error> guard(err);
    Reader r{data};
    if(!decode_value<default_config<Config>>(r, out)) {
        return std::unexpected(std::move(err));
    }
    if(r.pos != data.size()) {
        return std::unexpected(rich_error(std::string(error_message(error_kind::TrailingBytes))));
    }
    return {};
}

template <typename Config = void, typename T>
auto from_bytes(std::span<const std::uint8_t> data, T& out) -> std::expected<void, bincode::error> {
    return from_bytes<Config>(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(data.data()), data.size()),
        out);
}

template <typename T, typename Config = void>
    requires std::default_initializable<T>
auto from_bytes(std::span<const std::byte> data) -> std::expected<T, bincode::error> {
    T value{};
    auto result = from_bytes<Config>(data, value);
    if(!result) {
        return std::unexpected(result.error());
    }
    return value;
}

template <typename T, typename Config = void>
    requires std::default_initializable<T>
auto from_bytes(std::span<const std::uint8_t> data) -> std::expected<T, bincode::error> {
    T value{};
    auto result = from_bytes<Config>(data, value);
    if(!result) {
        return std::unexpected(result.error());
    }
    return value;
}

}  // namespace kota::codec::bincode

namespace kota::codec {

// std::monostate is null_like, but in bincode variant payloads it should read nothing
// (the old Deserializer skipped monostate payloads entirely).
template <typename Config>
struct deserialize_visit<bincode::Reader, std::monostate, Config> {
    static bool visit(bincode::Reader& /*vis*/, std::monostate& /*value*/) {
        return true;
    }
};

}  // namespace kota::codec
