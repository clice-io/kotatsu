#pragma once

#include <algorithm>
#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "kota/support/expected_try.h"
#include "kota/support/numeric.h"
#include "kota/codec/bincode/type.h"
#include "kota/codec/dyn/document.h"
#include "kota/codec/visit/config.h"
#include "kota/codec/visit/context.h"
#include "kota/codec/visit/decode.h"

namespace kota::codec::bincode {

namespace detail {

// The messages a bincode decode fails with.
constexpr inline std::string_view unexpected_eof = "unexpected eof";
constexpr inline std::string_view type_mismatch = "type mismatch";
constexpr inline std::string_view number_out_of_range = "number out of range";
constexpr inline std::string_view trailing_bytes = "trailing bytes";
constexpr inline std::string_view invalid_length = "invalid length";

}  // namespace detail

struct Reader;

struct SeqAccess {
    Reader& r;
    std::uint64_t count;
    std::uint64_t idx = 0;

    bool has_element();

    /// How many elements to make room for: the count, cut to the bytes left
    /// (see Reader::size_hint).
    std::size_t size_hint() const;

    template <typename F>
    bool visit_element(F&& f);
};

struct MapAccess {
    Reader& r;
    std::uint64_t count;
    std::uint64_t idx = 0;

    bool has_entry();

    /// As SeqAccess::size_hint, for entries.
    std::size_t size_hint() const;

    template <typename KF, typename VF>
    bool visit_entry(KF&& key_reader, VF&& value_reader);
};

/// Mirror of Writer: consumes `data` front to back in bincode's
/// little-endian layout, failing with "unexpected eof", "type mismatch",
/// "number out of range" or "invalid length" through the scoped error
/// context. Every read is length-checked via check_remaining before touching
/// the buffer; integers are read at their widened 8-byte size and narrowed
/// back into the target type with a range check. from_bytes additionally
/// rejects buffers with bytes left over after the root value ("trailing
/// bytes").
struct Reader {
    std::span<const std::byte> data;
    std::size_t pos = 0;
    using format = bincode::format;
    constexpr static bool human_readable = false;

    bool check_remaining(std::uint64_t n) {
        assert(pos <= data.size());
        if(n > data.size() - pos) {
            return fail(detail::unexpected_eof);
        }
        return true;
    }

    std::uint8_t read_u8() {
        auto byte = std::to_integer<std::uint8_t>(data[pos]);
        ++pos;
        return byte;
    }

    template <typename T>
        requires std::integral<T>
    T read_le() {
        using unsigned_t = std::make_unsigned_t<T>;
        unsigned_t raw = 0;
        for(std::size_t i = 0; i < sizeof(unsigned_t); ++i) {
            auto byte = std::to_integer<std::uint8_t>(data[pos + i]);
            raw |= (static_cast<unsigned_t>(byte) << (i * 8));
        }
        pos += sizeof(unsigned_t);
        if constexpr(std::signed_integral<T>) {
            return std::bit_cast<T>(raw);
        } else {
            return static_cast<T>(raw);
        }
    }

    /// A length or an element count, as Writer::write_length writes it.
    bool read_length(std::uint64_t& out) {
        KOTA_CODEC_TRY(check_remaining(1));
        auto marker = read_u8();
        using enum detail::LengthMarker;
        if(marker < std::to_underlying(U16)) {
            out = marker;
            return true;
        }
        switch(static_cast<detail::LengthMarker>(marker)) {
            case U16: return read_wide<std::uint16_t>(out);
            case U32: return read_wide<std::uint32_t>(out);
            case U64: return read_wide<std::uint64_t>(out);
        }
        return fail(detail::invalid_length);
    }

    bool visit_bool(bool& out) {
        KOTA_CODEC_TRY(check_remaining(1));
        auto byte = read_u8();
        if(byte > 1U) {
            return fail(detail::type_mismatch);
        }
        out = byte == 1U;
        return true;
    }

    template <typename T>
    bool visit_int(T& out) {
        KOTA_CODEC_TRY(check_remaining(sizeof(std::int64_t)));
        auto raw = read_le<std::int64_t>();
        if(!kota::narrow_int(raw, out)) {
            return fail(detail::number_out_of_range);
        }
        return true;
    }

    template <typename T>
    bool visit_uint(T& out) {
        KOTA_CODEC_TRY(check_remaining(sizeof(std::uint64_t)));
        auto raw = read_le<std::uint64_t>();
        if(!kota::narrow_int(raw, out)) {
            return fail(detail::number_out_of_range);
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
        std::uint64_t length = 0;
        KOTA_CODEC_TRY(read_length(length));
        KOTA_CODEC_TRY(check_remaining(length));
        auto len = static_cast<std::size_t>(length);
        const auto* begin = reinterpret_cast<const char*>(data.data() + pos);
        // assign writes into the string's own buffer, where a new string
        // would allocate one.
        if constexpr(requires { out.assign(begin, len); }) {
            out.assign(begin, len);
        } else {
            out = T(begin, begin + len);
        }
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
        std::uint64_t length = 0;
        KOTA_CODEC_TRY(read_length(length));
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
            return fail(detail::type_mismatch);
        }
        return body(*this);
    }

    bool visit_null() {
        KOTA_CODEC_TRY(check_remaining(1));
        if(read_u8() != 0x00) {
            return fail(detail::type_mismatch);
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
        std::uint64_t count = 0;
        KOTA_CODEC_TRY(read_length(count));
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
        std::uint64_t count = 0;
        KOTA_CODEC_TRY(read_length(count));
        MapAccess ctx{*this, count};
        return body(ctx);
    }

    template <typename Body>
    bool visit_variant(Body&& body) {
        KOTA_CODEC_TRY(check_remaining(sizeof(std::uint32_t)));
        auto index = read_le<std::uint32_t>();
        return body(static_cast<std::size_t>(index), *this);
    }

    /// How many of `count` elements to make room for before reading them:
    /// no more than the bytes left, so that a count the input forged makes
    /// room for no more elements than it has bytes. Zero-width elements
    /// take none, but making room for them costs nothing.
    std::size_t size_hint(std::uint64_t count) const {
        return static_cast<std::size_t>(std::min<std::uint64_t>(count, data.size() - pos));
    }

private:
    template <typename Wide>
    bool read_wide(std::uint64_t& out) {
        KOTA_CODEC_TRY(check_remaining(sizeof(Wide)));
        out = read_le<Wide>();
        return true;
    }

    static bool fail(std::string_view message) {
        return scoped_context<rich_error>::fail(rich_error(std::string(message)));
    }
};

inline bool SeqAccess::has_element() {
    return idx < count;
}

inline std::size_t SeqAccess::size_hint() const {
    return r.size_hint(count);
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

inline std::size_t MapAccess::size_hint() const {
    return r.size_hint(count);
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
auto from_bytes(std::span<const std::byte> data, T& out) -> std::expected<void, rich_error> {
    Reader r{data};
    KOTA_EXPECTED_TRY(codec::detail::run_decode<Config>(r, out));
    if(r.pos != data.size()) {
        return std::unexpected(rich_error(std::string(detail::trailing_bytes)));
    }
    return {};
}

template <typename Config = void, typename T>
auto from_bytes(std::span<const std::uint8_t> data, T& out) -> std::expected<void, rich_error> {
    return from_bytes<Config>(std::as_bytes(data), out);
}

template <typename T, typename Config = void>
    requires std::is_default_constructible_v<T>
auto from_bytes(std::span<const std::byte> data) -> std::expected<T, rich_error> {
    auto value = T();
    KOTA_EXPECTED_TRY(from_bytes<Config>(data, value));
    return value;
}

template <typename T, typename Config = void>
    requires std::is_default_constructible_v<T>
auto from_bytes(std::span<const std::uint8_t> data) -> std::expected<T, rich_error> {
    return from_bytes<T, Config>(std::as_bytes(data));
}

}  // namespace kota::codec::bincode

namespace kota::codec {

// std::monostate reads no byte, since the encoder writes none for it.
template <typename Config>
struct deserialize_visit<bincode::Reader, std::monostate, Config> {
    static bool visit(bincode::Reader& /*vis*/, std::monostate& /*value*/) {
        return true;
    }
};

/// A dyn::Value, read by the ValueKind byte its encoder wrote first (see
/// bincode::format). Arrays and objects are filled from an explicit stack
/// rather than by recursion, and a failed read releases what it built the
/// same way, so no input can run the call stack out; nesting is still
/// bounded, as simdjson bounds JSON's.
template <typename Config>
struct deserialize_visit<bincode::Reader, dyn::Value, Config> {
    constexpr static std::size_t max_depth = 1024;

    static bool visit(bincode::Reader& vis, dyn::Value& root) {
        if(fill(vis, root)) {
            return true;
        }
        release(root);
        return false;
    }

private:
    static bool fill(bincode::Reader& vis, dyn::Value& root) {
        /// A container still being filled: the next child's index, and how
        /// many children are left to read.
        struct Open {
            dyn::Value* node;
            std::size_t index;
            std::uint64_t remaining;
        };

        std::vector<Open> open;
        dyn::Value* target = &root;
        while(true) {
            if(open.size() == max_depth) {
                return fail_inside(
                    open,
                    rich_error(std::format("dyn::Value nested deeper than {} levels", max_depth)));
            }
            std::uint64_t children = 0;
            if(!read_node(vis, *target, children)) {
                return fail_inside(open);
            }
            if(children != 0) {
                open.push_back({.node = target, .index = 0, .remaining = children});
            }
            while(!open.empty() && open.back().remaining == 0) {
                open.pop_back();
            }
            if(open.empty()) {
                return true;
            }
            // Only the innermost container grows; every pointer on the stack
            // is to an ancestor of the slot being filled, which no insertion
            // moves.
            auto& parent = open.back();
            --parent.remaining;
            ++parent.index;
            if(auto* array = parent.node->get_array()) {
                target = &array->emplace_back();
            } else {
                std::string key;
                if(!decode_value<Config>(vis, key)) {
                    return fail_inside(open);
                }
                auto* object = parent.node->get_object();
                object->insert(std::move(key), dyn::Value());
                target = &std::prev(object->end())->second;
            }
        }
    }

    /// Reads one value: a scalar or string whole, an array or object as an
    /// empty container whose child count is returned.
    static bool read_node(bincode::Reader& vis, dyn::Value& out, std::uint64_t& children) {
        KOTA_CODEC_TRY(vis.check_remaining(1));
        switch(const auto kind = vis.read_u8(); static_cast<dyn::ValueKind>(kind)) {
            case dyn::ValueKind::null_value: out = dyn::Value(nullptr); return true;
            case dyn::ValueKind::boolean: return read_as<bool>(vis, out);
            case dyn::ValueKind::signed_int: return read_as<std::int64_t>(vis, out);
            case dyn::ValueKind::unsigned_int: return read_as<std::uint64_t>(vis, out);
            case dyn::ValueKind::floating: return read_as<double>(vis, out);
            case dyn::ValueKind::string: return read_as<std::string>(vis, out);
            case dyn::ValueKind::array:
                KOTA_CODEC_TRY(vis.read_length(children));
                out = dyn::Value(dyn::Array{});
                return true;
            case dyn::ValueKind::object:
                KOTA_CODEC_TRY(vis.read_length(children));
                out = dyn::Value(dyn::Object{});
                return true;
            default:
                return scoped_context<rich_error>::fail(
                    rich_error(std::format("invalid dyn::Value kind {}", kind)));
        }
    }

    template <typename T>
    static bool read_as(bincode::Reader& vis, dyn::Value& out) {
        T held{};
        KOTA_CODEC_TRY(decode_value<Config>(vis, held));
        out = dyn::Value(std::move(held));
        return true;
    }

    /// Destroys a tree one node at a time: a Value's own destructor recurses
    /// through its children.
    static void release(dyn::Value& root) {
        std::vector<dyn::Value> pending;
        pending.push_back(std::move(root));
        root = dyn::Value(nullptr);
        while(!pending.empty()) {
            dyn::Value node = std::move(pending.back());
            pending.pop_back();
            if(auto* array = node.get_array()) {
                for(auto& child: *array) {
                    pending.push_back(std::move(child));
                }
            } else if(auto* object = node.get_object()) {
                for(auto& entry: *object) {
                    pending.push_back(std::move(entry.second));
                }
            }
        }
    }

    /// Fails the read in progress: with error when given (the failed read
    /// set it otherwise), and with the index of each open container's
    /// current child on the path, innermost first.
    template <typename Open>
    static bool fail_inside(const std::vector<Open>& open, std::optional<rich_error> error = {}) {
        if(error) {
            scoped_context<rich_error>::fail(std::move(*error));
        }
        for(auto frame = open.rbegin(); frame != open.rend(); ++frame) {
            detail::trace_path<Config>(false, frame->index - 1);
        }
        return false;
    }
};

/// A bare object: its entry count, then each key and Value, the way the
/// generic map encoding writes it. Object keeps its entries in order with
/// insert(), which the generic map decoding does not know.
template <typename Config>
struct deserialize_visit<bincode::Reader, dyn::Object, Config> {
    static bool visit(bincode::Reader& vis, dyn::Object& object) {
        object.clear();
        return vis.visit_map(object, [&](auto& entries) -> bool {
            std::size_t idx = 0;
            while(entries.has_entry()) {
                std::string key;
                dyn::Value item;
                bool ok = entries.visit_entry(
                    [&](auto& kr) -> bool { return decode_value<Config>(kr, key); },
                    [&](auto& vr) -> bool { return decode_value<Config>(vr, item); });
                KOTA_CODEC_TRY(detail::trace_path<Config>(ok, idx));
                object.insert(std::move(key), std::move(item));
                ++idx;
            }
            return true;
        });
    }
};

}  // namespace kota::codec
