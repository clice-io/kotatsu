#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <string>
#include <string_view>

#include "kotatsu/support/expected_try.h"
#include "kotatsu/codec/context.h"
#include "kotatsu/codec/dyn/document.h"
#include "kotatsu/codec/encode.h"
#include "kotatsu/codec/format.h"
#include "kotatsu/codec/repr.h"

// The dyn backend is deliberately format-agnostic: its visitors declare no
// `format` tag, so format-scoped codec::repr specializations never apply here
// — only the format-neutral repr<T>. dyn::Value is the interchange DOM that
// other backends convert through, and a concrete format's repr leaking into
// it would bake that format's shape into every conversion.

namespace kotatsu::codec::dyn {

template <typename Format>
struct ValueWriter {
    dyn::Value& output;
    using format = Format;

    bool visit_bool(bool v) {
        output = dyn::Value(v);
        return true;
    }

    template <typename T>
    bool visit_int(T v) {
        output = dyn::Value(static_cast<std::int64_t>(v));
        return true;
    }

    template <typename T>
    bool visit_uint(T v) {
        output = dyn::Value(static_cast<std::uint64_t>(v));
        return true;
    }

    template <typename T>
    bool visit_float(T v) {
        output = dyn::Value(static_cast<double>(v));
        return true;
    }

    template <typename T>
    bool visit_str(const T& v) {
        output = dyn::Value(std::string(std::string_view(v)));
        return true;
    }

    template <typename T>
    bool visit_char(T v) {
        output = dyn::Value(char_to_utf8(v));
        return true;
    }

    template <typename T>
    bool visit_bytes(const T& v) {
        auto data = reinterpret_cast<const std::uint8_t*>(std::data(v));
        auto len = std::size(v);
        dyn::Array arr;
        for(std::size_t i = 0; i < len; ++i) {
            arr.push_back(dyn::Value(static_cast<std::uint64_t>(data[i])));
        }
        output = dyn::Value(std::move(arr));
        return true;
    }

    bool visit_null() {
        output = dyn::Value(nullptr);
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

template <typename Format>
struct StructWriter {
    dyn::Object& obj;

    template <typename Field, typename F>
    bool visit_field(Field field, F&& writer);
};

template <typename Format>
struct SeqWriter {
    dyn::Array& arr;

    template <typename F>
    bool visit_element(F&& writer);
};

template <typename Format>
struct MapWriter {
    dyn::Object& obj;

    template <typename KF, typename VF>
    bool visit_entry(KF&& key_fn, VF&& value_fn);
};

template <typename Format>
template <typename T, typename Body>
bool ValueWriter<Format>::visit_struct(const T&, Body&& body) {
    dyn::Object obj;
    StructWriter<Format> sw{obj};
    KOTATSU_CODEC_TRY(body(sw));
    output = dyn::Value(std::move(obj));
    return true;
}

template <typename Format>
template <typename Container, typename Body>
bool ValueWriter<Format>::visit_seq(const Container&, Body&& body) {
    dyn::Array arr;
    SeqWriter<Format> sw{arr};
    KOTATSU_CODEC_TRY(body(sw));
    output = dyn::Value(std::move(arr));
    return true;
}

template <typename Format>
template <typename Container, typename Body>
bool ValueWriter<Format>::visit_map(const Container&, Body&& body) {
    dyn::Object obj;
    MapWriter<Format> mw{obj};
    KOTATSU_CODEC_TRY(body(mw));
    output = dyn::Value(std::move(obj));
    return true;
}

template <typename Format>
template <typename T, typename Body>
bool ValueWriter<Format>::visit_tuple(const T& value, Body&& body) {
    return visit_seq(value, std::forward<Body>(body));
}

template <typename Format>
template <typename Field, typename F>
bool StructWriter<Format>::visit_field(Field field, F&& writer) {
    dyn::Value field_val;
    ValueWriter<Format> vw{field_val};
    KOTATSU_CODEC_TRY(writer(vw));
    obj.insert(std::string(field.name), std::move(field_val));
    return true;
}

template <typename Format>
template <typename F>
bool SeqWriter<Format>::visit_element(F&& writer) {
    dyn::Value elem_val;
    ValueWriter<Format> vw{elem_val};
    KOTATSU_CODEC_TRY(writer(vw));
    arr.push_back(std::move(elem_val));
    return true;
}

template <typename Format>
template <typename KF, typename VF>
bool MapWriter<Format>::visit_entry(KF&& key_fn, VF&& value_fn) {
    std::string key;
    MapKeyWriter<StringKeySink, Format> kw{{key}};
    KOTATSU_CODEC_TRY(key_fn(kw));
    dyn::Value val;
    ValueWriter<Format> vw{val};
    KOTATSU_CODEC_TRY(value_fn(vw));
    obj.insert(std::move(key), std::move(val));
    return true;
}

/// Encodes `value` as a dyn::Value DOM tree (the format-neutral
/// interchange representation; see the header comment) under the format F.
template <typename F = codec::format, typename T>
auto to_dyn(const T& value, const encode_options& options = {})
    -> std::expected<dyn::Value, rich_error> {
    dyn::Value result;
    ValueWriter<F> vis{result};
    KOTATSU_EXPECTED_TRY(codec::detail::run_encode<F>(vis, value, options));
    return result;
}

}  // namespace kotatsu::codec::dyn

namespace kotatsu::codec {

template <typename Format>
struct serialize_visit<dyn::ValueWriter<Format>, dyn::Value> {
    static bool visit(dyn::ValueWriter<Format>& vis, const dyn::Value& value) {
        vis.output = value;
        return true;
    }
};

template <typename Format>
struct serialize_visit<dyn::ValueWriter<Format>, dyn::Array> {
    static bool visit(dyn::ValueWriter<Format>& vis, const dyn::Array& value) {
        vis.output = dyn::Value(value);
        return true;
    }
};

template <typename Format>
struct serialize_visit<dyn::ValueWriter<Format>, dyn::Object> {
    static bool visit(dyn::ValueWriter<Format>& vis, const dyn::Object& value) {
        vis.output = dyn::Value(value);
        return true;
    }
};

}  // namespace kotatsu::codec
