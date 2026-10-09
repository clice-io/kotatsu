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

#include "kota/support/expected_try.h"
#include "kota/codec/json/string_builder.h"
#include "kota/codec/json/type.h"
#include "kota/codec/visit/common.h"
#include "kota/codec/visit/config.h"
#include "kota/codec/visit/context.h"
#include "kota/codec/visit/encode.h"
#include "kota/codec/visit/map_key.h"

namespace kota::codec::json {

struct ValueWriter {
    StringBuilder& builder;
    using format = json::format;
    constexpr static bool human_readable = true;

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
        double d = static_cast<double>(v);
        if(std::isfinite(d)) {
            builder.append_number(d);
        } else {
            builder.append_null();
        }
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

/// `,"name":`, the key of a member that follows another, for a field whose
/// name is known at compile time.
template <typename Field>
constexpr auto member_key = [] {
    constexpr std::string_view name = FieldName<Field>::value;
    std::array<char, escaped_size(name) + 4> key{};
    key[0] = ',';
    key[1] = '"';
    auto* end = escape_to(name, key.data() + 2);
    end[0] = '"';
    end[1] = ':';
    return key;
}();

}  // namespace detail

struct StructWriter {
    StringBuilder& builder;
    bool first = true;

    template <typename F>
    bool visit_field(std::size_t /*index*/, std::string_view name, F&& writer);

    /// A field's key, written as one piece the compiler spells out.
    template <typename Field, typename F>
    bool visit_field(std::size_t /*index*/, FieldName<Field> /*name*/, F&& writer);
};

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

struct MapWriter {
    StringBuilder& builder;
    bool first = true;

    template <typename KF, typename VF>
    bool visit_entry(KF&& key_fn, VF&& value_fn);
};

template <typename T, typename Body>
bool ValueWriter::visit_struct(const T&, Body&& body) {
    builder.put('{');
    StructWriter sw{builder};
    KOTA_CODEC_TRY(body(sw));
    builder.put('}');
    return true;
}

template <typename Container, typename Body>
bool ValueWriter::visit_seq(const Container&, Body&& body) {
    builder.put('[');
    SeqWriter sw{builder};
    KOTA_CODEC_TRY(body(sw));
    builder.put(']');
    return true;
}

template <typename Container, typename Body>
bool ValueWriter::visit_map(const Container&, Body&& body) {
    builder.put('{');
    MapWriter mw{builder};
    KOTA_CODEC_TRY(body(mw));
    builder.put('}');
    return true;
}

template <typename T, typename Body>
bool ValueWriter::visit_tuple(const T& value, Body&& body) {
    return visit_seq(value, std::forward<Body>(body));
}

template <typename F>
bool StructWriter::visit_field(std::size_t /*index*/, std::string_view name, F&& writer) {
    if(!first)
        builder.put(',');
    first = false;
    builder.append_string(name);
    builder.put(':');
    ValueWriter vw{builder};
    return writer(vw);
}

template <typename Field, typename F>
bool StructWriter::visit_field(std::size_t /*index*/, FieldName<Field> /*name*/, F&& writer) {
    constexpr auto& key = detail::member_key<Field>;
    if(first) {
        builder.append_raw<key.size() - 1>(key.data() + 1);
    } else {
        builder.append_raw<key.size()>(key.data());
    }
    first = false;
    ValueWriter vw{builder};
    return writer(vw);
}

template <typename F>
bool SeqWriter::visit_element(F&& writer) {
    if(!first)
        builder.put(',');
    first = false;
    ValueWriter vw{builder};
    return writer(vw);
}

template <typename KF, typename VF>
bool MapWriter::visit_entry(KF&& key_fn, VF&& value_fn) {
    if(!first)
        builder.put(',');
    first = false;
    MapKeyWriter<KeySink, format> kw{{builder}};
    KOTA_CODEC_TRY(key_fn(kw));
    builder.put(':');
    ValueWriter vw{builder};
    return value_fn(vw);
}

/// The room to_string starts with, unless told how much.
constexpr std::size_t default_capacity = 1024;

/// Encodes `value` as compact JSON text; use json::prettify for indented
/// output.
template <typename Config = void, typename T>
auto to_string(const T& value, std::optional<std::size_t> initial_capacity = std::nullopt)
    -> std::expected<std::string, rich_error> {
    StringBuilder builder(initial_capacity.value_or(default_capacity));
    ValueWriter vis{builder};
    KOTA_EXPECTED_TRY(codec::detail::run_encode<Config>(vis, value));
    return std::move(builder).take();
}

}  // namespace kota::codec::json
