#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <string>
#include <string_view>

#include "kota/codec/dyn/document.h"
#include "kota/codec/visit/common.h"
#include "kota/codec/visit/config.h"
#include "kota/codec/visit/context.h"
#include "kota/codec/visit/encode.h"
#include "kota/codec/visit/map_key.h"

// The dyn backend is deliberately format-agnostic: its visitors declare no
// `format` tag, so format-scoped meta::repr specializations never apply here
// — only the format-neutral repr<T>. dyn::Value is the interchange DOM that
// other backends convert through, and a concrete format's repr leaking into
// it would bake that format's shape into every conversion.

namespace kota::codec::dyn {

struct ValueWriter;
struct StructWriter;
struct SeqWriter;
struct MapWriter;

struct ValueWriter {
    dyn::Value& output;
    using error_type = rich_error;
    constexpr static bool human_readable = true;

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
        double d = static_cast<double>(v);
        if(std::isfinite(d)) {
            output = dyn::Value(d);
        } else {
            output = dyn::Value(nullptr);
        }
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
    inline bool visit_struct(const T&, Body&& body);

    template <typename Container, typename Body>
    inline bool visit_seq(const Container&, Body&& body);

    template <typename Container, typename Body>
    inline bool visit_map(const Container&, Body&& body);

    template <typename T, typename Body>
    inline bool visit_tuple(const T&, Body&& body);
};

struct StructWriter {
    dyn::Object& obj;
    using error_type = rich_error;

    template <typename F>
    inline bool visit_field(std::size_t /*index*/, std::string_view name, F&& writer);
};

struct SeqWriter {
    dyn::Array& arr;

    template <typename F>
    inline bool visit_element(F&& writer);
};

struct MapWriter {
    dyn::Object& obj;

    template <typename KF, typename VF>
    inline bool visit_entry(KF&& key_fn, VF&& value_fn);
};

template <typename T, typename Body>
bool ValueWriter::visit_struct(const T&, Body&& body) {
    dyn::Object obj;
    StructWriter sw{obj};
    KOTA_CODEC_TRY(body(sw));
    output = dyn::Value(std::move(obj));
    return true;
}

template <typename Container, typename Body>
bool ValueWriter::visit_seq(const Container&, Body&& body) {
    dyn::Array arr;
    SeqWriter sw{arr};
    KOTA_CODEC_TRY(body(sw));
    output = dyn::Value(std::move(arr));
    return true;
}

template <typename Container, typename Body>
bool ValueWriter::visit_map(const Container&, Body&& body) {
    dyn::Object obj;
    MapWriter mw{obj};
    KOTA_CODEC_TRY(body(mw));
    output = dyn::Value(std::move(obj));
    return true;
}

template <typename T, typename Body>
bool ValueWriter::visit_tuple(const T&, Body&& body) {
    dyn::Array arr;
    SeqWriter sw{arr};
    KOTA_CODEC_TRY(body(sw));
    output = dyn::Value(std::move(arr));
    return true;
}

template <typename F>
bool StructWriter::visit_field(std::size_t /*index*/, std::string_view name, F&& writer) {
    dyn::Value field_val;
    ValueWriter vw{field_val};
    KOTA_CODEC_TRY(writer(vw));
    obj.insert(std::string(name), std::move(field_val));
    return true;
}

template <typename F>
bool SeqWriter::visit_element(F&& writer) {
    dyn::Value elem_val;
    ValueWriter vw{elem_val};
    KOTA_CODEC_TRY(writer(vw));
    arr.push_back(std::move(elem_val));
    return true;
}

template <typename KF, typename VF>
bool MapWriter::visit_entry(KF&& key_fn, VF&& value_fn) {
    std::string key;
    MapKeyWriter<StringKeySink> kw{{key}};
    KOTA_CODEC_TRY(key_fn(kw));
    dyn::Value val;
    ValueWriter vw{val};
    KOTA_CODEC_TRY(value_fn(vw));
    obj.insert(std::move(key), std::move(val));
    return true;
}

/// Encodes `value` as a dyn::Value DOM tree (the format-neutral
/// interchange representation; see the header comment).
template <typename Config = void, typename T>
auto to_dyn(const T& value) -> std::expected<dyn::Value, rich_error> {
    rich_error err;
    scoped_context<rich_error> guard(err);
    dyn::Value result;
    ValueWriter vis{result};
    if(!encode_value<default_config<Config>>(vis, value)) {
        return std::unexpected(std::move(err));
    }
    return result;
}

}  // namespace kota::codec::dyn

namespace kota::codec {

template <typename Config>
struct serialize_visit<dyn::ValueWriter, dyn::Value, Config> {
    static bool visit(dyn::ValueWriter& vis, const dyn::Value& value) {
        vis.output = value;
        return true;
    }
};

template <typename Config>
struct serialize_visit<dyn::ValueWriter, dyn::Array, Config> {
    static bool visit(dyn::ValueWriter& vis, const dyn::Array& value) {
        vis.output = dyn::Value(value);
        return true;
    }
};

template <typename Config>
struct serialize_visit<dyn::ValueWriter, dyn::Object, Config> {
    static bool visit(dyn::ValueWriter& vis, const dyn::Object& value) {
        vis.output = dyn::Value(value);
        return true;
    }
};

}  // namespace kota::codec
