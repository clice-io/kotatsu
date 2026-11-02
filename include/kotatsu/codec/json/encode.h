#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "kotatsu/support/expected_try.h"
#include "kotatsu/codec/context.h"
#include "kotatsu/codec/encode.h"
#include "kotatsu/codec/format.h"
#include "kotatsu/codec/json/string_builder.h"
#include "kotatsu/codec/json/type.h"
#include "kotatsu/codec/repr.h"

namespace kotatsu::codec::json {

template <typename Format>
struct ValueWriter {
    StringBuilder& builder;
    using format = Format;
    constexpr static bool utf8 = true;

    bool visit_bool(bool v) {
        builder.append_bool(v);
        return true;
    }

    template <typename T>
    bool visit_int(T v) {
        builder.append_number(static_cast<std::int64_t>(v));
        return true;
    }

    template <typename T>
    bool visit_uint(T v) {
        builder.append_number(static_cast<std::uint64_t>(v));
        return true;
    }

    template <typename T>
    bool visit_float(T v) {
        // Judged as written: a finite long double beyond double's range is
        // an infinity in the document.
        double d = static_cast<double>(v);
        if(!std::isfinite(d)) {
            return scoped_context<rich_error>::fail(
                rich_error("JSON has no number for NaN or an infinity"));
        }
        builder.append_number(d);
        return true;
    }

    template <typename T>
    bool visit_str(const T& v) {
        builder.append_string(std::string_view(v));
        return true;
    }

    template <typename T>
    bool visit_char(T v) {
        builder.append_string(char_to_utf8(v));
        return true;
    }

    template <typename T>
    bool visit_bytes(const T& v) {
        auto data = reinterpret_cast<const std::uint8_t*>(std::data(v));
        auto len = std::size(v);
        builder.put('[');
        for(std::size_t i = 0; i < len; ++i) {
            if(i > 0)
                builder.put(',');
            builder.append_number(static_cast<std::uint64_t>(data[i]));
        }
        builder.put(']');
        return true;
    }

    bool visit_null() {
        builder.append_null();
        return true;
    }

    template <typename T, typename Body>
    bool visit_struct(const T&, Body&& body);

    template <typename Container, typename Body>
    bool visit_seq(const Container&, Body&& body);

    template <typename Container, typename Body>
    bool visit_map(const Container&, Body&& body);

    template <typename T, typename Body>
    bool visit_tuple(const T&, Body&& body);
};

namespace detail {

/// `,"name":`, the key of a member that follows another, for a reflected
/// field, whose name is known at compile time.
template <typename Field>
constexpr auto member_key = [] {
    constexpr std::string_view name = Field::name;
    std::array<char, escaped_size(name) + 4> key{};
    key[0] = ',';
    key[1] = '"';
    auto* end = escape_to(name, key.data() + 2);
    end[0] = '"';
    end[1] = ':';
    return key;
}();

}  // namespace detail

template <typename Format>
struct StructWriter {
    StringBuilder& builder;
    bool first = true;

    template <typename F>
    bool visit_field(named_field field, F&& writer);

    /// A reflected field's key, written as one piece the compiler spells out.
    template <typename T, std::size_t I, typename Scope, typename F>
    bool visit_field(codec::field<T, I, Scope> field, F&& writer);
};

template <typename Format>
struct SeqWriter {
    StringBuilder& builder;
    bool first = true;

    template <typename F>
    bool visit_element(F&& writer);
};

/// Streams a rendered map key into the JSON output as a quoted, escaped
/// string.
struct KeySink {
    StringBuilder& builder;

    void emit(std::string_view key) {
        builder.append_string(key);
    }
};

template <typename Format>
struct MapWriter {
    StringBuilder& builder;
    bool first = true;

    template <typename KF, typename VF>
    bool visit_entry(KF&& key_fn, VF&& value_fn);
};

template <typename Format>
template <typename T, typename Body>
bool ValueWriter<Format>::visit_struct(const T&, Body&& body) {
    builder.put('{');
    StructWriter<Format> sw{builder};
    KOTATSU_CODEC_TRY(body(sw));
    builder.put('}');
    return true;
}

template <typename Format>
template <typename Container, typename Body>
bool ValueWriter<Format>::visit_seq(const Container&, Body&& body) {
    builder.put('[');
    SeqWriter<Format> sw{builder};
    KOTATSU_CODEC_TRY(body(sw));
    builder.put(']');
    return true;
}

template <typename Format>
template <typename Container, typename Body>
bool ValueWriter<Format>::visit_map(const Container&, Body&& body) {
    builder.put('{');
    MapWriter<Format> mw{builder};
    KOTATSU_CODEC_TRY(body(mw));
    builder.put('}');
    return true;
}

template <typename Format>
template <typename T, typename Body>
bool ValueWriter<Format>::visit_tuple(const T& value, Body&& body) {
    return visit_seq(value, std::forward<Body>(body));
}

template <typename Format>
template <typename F>
bool StructWriter<Format>::visit_field(named_field field, F&& writer) {
    if(!first)
        builder.put(',');
    first = false;
    builder.append_string(field.name);
    builder.put(':');
    ValueWriter<Format> vw{builder};
    return writer(vw);
}

template <typename Format>
template <typename T, std::size_t I, typename Scope, typename F>
bool StructWriter<Format>::visit_field(codec::field<T, I, Scope> /*field*/, F&& writer) {
    constexpr auto& key = detail::member_key<codec::field<T, I, Scope>>;
    if(first) {
        builder.append_raw<key.size() - 1>(key.data() + 1);
    } else {
        builder.append_raw<key.size()>(key.data());
    }
    first = false;
    ValueWriter<Format> vw{builder};
    return writer(vw);
}

template <typename Format>
template <typename F>
bool SeqWriter<Format>::visit_element(F&& writer) {
    if(!first)
        builder.put(',');
    first = false;
    ValueWriter<Format> vw{builder};
    return writer(vw);
}

template <typename Format>
template <typename KF, typename VF>
bool MapWriter<Format>::visit_entry(KF&& key_fn, VF&& value_fn) {
    if(!first)
        builder.put(',');
    first = false;
    MapKeyWriter<KeySink, Format, true> kw{{builder}};
    KOTATSU_CODEC_TRY(key_fn(kw));
    builder.put(':');
    ValueWriter<Format> vw{builder};
    return value_fn(vw);
}

/// The room to_string starts with, unless told how much.
constexpr std::size_t default_capacity = 1024;

/// Encodes `value` as compact JSON text under the format F; use json::prettify
/// for indented output.
template <typename F = json::format, typename T>
auto to_string(const T& value, const json::encode_options& options = {})
    -> std::expected<std::string, rich_error> {
    static_assert(std::derived_from<F, json::format>, "a JSON format extends json::format");
    StringBuilder builder(options.initial_capacity.value_or(default_capacity));
    ValueWriter<F> vis{builder};
    KOTATSU_EXPECTED_TRY(codec::detail::run_encode<F>(vis, value, options.common()));
    return std::move(builder).take();
}

}  // namespace kotatsu::codec::json
