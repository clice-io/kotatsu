#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

#include "kotatsu/support/expected_try.h"
#include "kotatsu/codec/context.h"
#include "kotatsu/codec/encode.h"
#include "kotatsu/codec/format.h"
#include "kotatsu/codec/repr.h"
#include "kotatsu/codec/toml/type.h"

namespace kotatsu::codec::toml {

struct TableSink {
    Table& tbl;
    std::string key;

    template <typename V>
    bool emit(V&& v) {
        tbl.insert_or_assign(key, std::forward<V>(v));
        return true;
    }

    bool emit_null() {
        return true;
    }
};

struct ArraySink {
    Array& arr;

    template <typename V>
    bool emit(V&& v) {
        arr.push_back(std::forward<V>(v));
        return true;
    }

    bool emit_null() {
        return scoped_context<rich_error>::fail(rich_error("TOML array does not support null"));
    }
};

/// Receives the document root: a table-shaped value becomes the root table
/// itself rather than landing under a key.
struct RootSink {
    Table& root;

    template <typename V>
    bool emit(V&& v) {
        if constexpr(std::is_same_v<std::remove_cvref_t<V>, Table>) {
            root = std::forward<V>(v);
            return true;
        } else {
            return scoped_context<rich_error>::fail(
                rich_error("top-level TOML value must be a table"));
        }
    }

    bool emit_null() {
        return true;
    }
};

template <typename Format, typename Sink>
struct ValueWriter {
    Sink sink;
    using format = Format;

    bool visit_null() {
        return sink.emit_null();
    }

    bool visit_bool(bool v) {
        return sink.emit(v);
    }

    template <typename T>
    bool visit_int(T v) {
        return sink.emit(static_cast<std::int64_t>(v));
    }

    template <typename T>
    bool visit_uint(T v) {
        auto wide = static_cast<std::uint64_t>(v);
        if(wide > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) {
            return scoped_context<rich_error>::fail(rich_error("uint64 exceeds int64 range"));
        }
        return sink.emit(static_cast<std::int64_t>(wide));
    }

    template <typename T>
    bool visit_float(T v) {
        return sink.emit(static_cast<double>(v));
    }

    template <typename T>
    bool visit_str(const T& v) {
        return sink.emit(std::string(std::string_view(v)));
    }

    template <typename T>
    bool visit_char(T v) {
        // A lone octet above 0x7F is not UTF-8, and toml++ would write an
        // empty string in its place.
        return sink.emit(char_to_utf8(v));
    }

    template <typename T>
    bool visit_bytes(const T& v) {
        Array byte_arr;
        auto data = reinterpret_cast<const std::uint8_t*>(std::data(v));
        auto len = std::size(v);
        for(std::size_t i = 0; i < len; ++i)
            byte_arr.push_back(static_cast<std::int64_t>(data[i]));
        return sink.emit(std::move(byte_arr));
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
using TableValueWriter = ValueWriter<Format, TableSink>;
template <typename Format>
using ArrayValueWriter = ValueWriter<Format, ArraySink>;

template <typename Format>
struct TableWriter {
    Table& tbl;

    template <typename Field, typename F>
    bool visit_field(Field field, F&& writer);
};

template <typename Format>
struct ArraySeqWriter {
    Array& arr;

    template <typename F>
    bool visit_element(F&& writer);
};

template <typename Format>
struct MapWriter {
    Table& tbl;

    template <typename KF, typename VF>
    bool visit_entry(KF&& key_fn, VF&& value_fn);
};

template <typename Format, typename Sink>
template <typename T, typename Body>
bool ValueWriter<Format, Sink>::visit_struct(const T&, Body&& body) {
    Table child;
    TableWriter<Format> tw{child};
    KOTATSU_CODEC_TRY(body(tw));
    return sink.emit(std::move(child));
}

template <typename Format, typename Sink>
template <typename Container, typename Body>
bool ValueWriter<Format, Sink>::visit_seq(const Container&, Body&& body) {
    Array arr;
    ArraySeqWriter<Format> sw{arr};
    KOTATSU_CODEC_TRY(body(sw));
    return sink.emit(std::move(arr));
}

template <typename Format, typename Sink>
template <typename Container, typename Body>
bool ValueWriter<Format, Sink>::visit_map(const Container&, Body&& body) {
    Table child;
    MapWriter<Format> mw{child};
    KOTATSU_CODEC_TRY(body(mw));
    return sink.emit(std::move(child));
}

template <typename Format, typename Sink>
template <typename T, typename Body>
bool ValueWriter<Format, Sink>::visit_tuple(const T& value, Body&& body) {
    return visit_seq(value, std::forward<Body>(body));
}

template <typename Format>
template <typename Field, typename F>
bool TableWriter<Format>::visit_field(Field field, F&& writer) {
    TableValueWriter<Format> vw{
        {tbl, std::string(field.name)}
    };
    return writer(vw);
}

template <typename Format>
template <typename F>
bool ArraySeqWriter<Format>::visit_element(F&& writer) {
    ArrayValueWriter<Format> vw{{arr}};
    return writer(vw);
}

template <typename Format>
template <typename KF, typename VF>
bool MapWriter<Format>::visit_entry(KF&& key_fn, VF&& value_fn) {
    std::string key;
    MapKeyWriter<StringKeySink, Format> kw{{key}};
    KOTATSU_CODEC_TRY(key_fn(kw));
    TableValueWriter<Format> vw{
        {tbl, std::move(key)}
    };
    return value_fn(vw);
}

/// Encodes `value` as a toml::Table DOM under the format F (to_string renders
/// it as text).
template <typename F = toml::format, typename T>
auto to_toml(const T& value, const encode_options& options = {})
    -> std::expected<Table, rich_error> {
    static_assert(std::derived_from<F, toml::format>, "a TOML format extends toml::format");
    // Root routing follows the representation the codec dispatch resolves
    // (annotations and toml-scoped codec::repr included), not the declared
    // type: a struct whose repr is a scalar is boxed under the root key, and
    // a repr that resolves to a table shape becomes the root table.
    if constexpr(detail::nullable_root_v<T, F>) {
        if(value) {
            auto engaged = to_toml<F>(*value, options);
            // A null root is the empty document (TOML has no null), so an
            // engaged pointee that serializes to an empty table — a non-null
            // pointer to an empty map, say — would decode back as null. Fail
            // loudly instead of losing the engaged state silently.
            if(engaged && engaged->empty()) {
                return std::unexpected(
                    rich_error("engaged nullable root serializes to an empty TOML document, "
                               "indistinguishable from null"));
            }
            return engaged;
        }
        return Table{};
    } else {
        Table root;
        if constexpr(detail::root_table_v<T, F>) {
            ValueWriter<F, RootSink> vw{{root}};
            KOTATSU_EXPECTED_TRY(codec::detail::run_encode<F>(vw, value, options));
        } else {
            TableValueWriter<F> vw{
                {root, std::string(detail::boxed_root_key)}
            };
            KOTATSU_EXPECTED_TRY(codec::detail::run_encode<F>(vw, value, options));
        }
        return root;
    }
}

/// Encodes `value` as TOML text (to_toml rendered through toml++).
template <typename F = toml::format, typename T>
auto to_string(const T& value, const encode_options& options = {})
    -> std::expected<std::string, rich_error> {
    KOTATSU_EXPECTED_TRY_V(auto table, to_toml<F>(value, options));
    std::ostringstream out;
    out << table;
    return out.str();
}

}  // namespace kotatsu::codec::toml
