#pragma once

#include <charconv>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <system_error>

#include "kota/support/numeric.h"
#include "kota/codec/visit/context.h"

namespace kota::codec {

// Map keys for backends whose native maps take only string keys (json, toml,
// dyn): integer keys are rendered as their decimal string on encode and
// parsed back on decode. Format is the backend's format tag — void for a
// format-agnostic backend — so format-scoped meta::repr specializations
// resolve for key types the same way they do for values.

/// Decodes a map key from its string form.
template <typename Format = void>
struct MapKeyReader {
    std::string_view str;
    using format = Format;

    template <typename T>
    bool visit_str(T& out) {
        out = T(str);
        return true;
    }

    template <typename T>
    bool visit_int(T& out) {
        return parse<std::int64_t>("integer", out);
    }

    template <typename T>
    bool visit_uint(T& out) {
        return parse<std::uint64_t>("unsigned integer", out);
    }

private:
    template <typename Wide, typename T>
    bool parse(std::string_view target, T& out) {
        Wide v = 0;
        auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), v);
        if(ec != std::errc{} || ptr != str.data() + str.size()) {
            return scoped_context<rich_error>::fail(
                rich_error(std::format("cannot parse map key '{}' as {}", str, target)));
        }
        if(!kota::narrow_int(v, out)) {
            return scoped_context<rich_error>::fail(
                rich_error(std::format("map key '{}' out of {} range", str, target)));
        }
        return true;
    }
};

/// Encodes a map key into its string form, handing the result to Sink::emit
/// as a string_view that is only valid for the duration of that call.
template <typename Sink, typename Format = void>
struct MapKeyWriter {
    Sink sink;
    using format = Format;

    template <typename T>
    bool visit_str(const T& v) {
        sink.emit(std::string_view(v));
        return true;
    }

    template <typename T>
    bool visit_int(T v) {
        sink.emit(std::to_string(static_cast<std::int64_t>(v)));
        return true;
    }

    template <typename T>
    bool visit_uint(T v) {
        sink.emit(std::to_string(static_cast<std::uint64_t>(v)));
        return true;
    }
};

/// Sink for backends whose native map type is keyed by std::string.
struct StringKeySink {
    std::string& output;

    void emit(std::string_view key) {
        output = key;
    }
};

}  // namespace kota::codec
