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

#include "kota/meta/type_info.h"
#include "kota/meta/type_kind.h"
#include "kota/codec/toml/type.h"
#include "kota/codec/visit/common.h"
#include "kota/codec/visit/config.h"
#include "kota/codec/visit/context.h"
#include "kota/codec/visit/encode.h"
#include "kota/codec/visit/map_key.h"

namespace kota::codec::toml {

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

template <typename Sink>
struct ValueWriter {
    Sink sink;
    using error_type = rich_error;
    using format = toml::format;
    constexpr static bool human_readable = true;

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

using TableValueWriter = ValueWriter<TableSink>;
using ArrayValueWriter = ValueWriter<ArraySink>;

struct TableWriter {
    Table& tbl;
    using error_type = rich_error;

    template <typename F>
    bool visit_field(std::size_t /*index*/, std::string_view name, F&& writer);
};

struct ArraySeqWriter {
    Array& arr;

    template <typename F>
    bool visit_element(F&& writer);
};

struct MapWriter {
    Table& tbl;

    template <typename KF, typename VF>
    bool visit_entry(KF&& key_fn, VF&& value_fn);
};

template <typename Sink>
template <typename T, typename Body>
bool ValueWriter<Sink>::visit_struct(const T&, Body&& body) {
    Table child;
    TableWriter tw{child};
    KOTA_CODEC_TRY(body(tw));
    return sink.emit(std::move(child));
}

template <typename Sink>
template <typename Container, typename Body>
bool ValueWriter<Sink>::visit_seq(const Container&, Body&& body) {
    Array arr;
    ArraySeqWriter sw{arr};
    KOTA_CODEC_TRY(body(sw));
    return sink.emit(std::move(arr));
}

template <typename Sink>
template <typename Container, typename Body>
bool ValueWriter<Sink>::visit_map(const Container&, Body&& body) {
    Table child;
    MapWriter mw{child};
    KOTA_CODEC_TRY(body(mw));
    return sink.emit(std::move(child));
}

template <typename Sink>
template <typename T, typename Body>
bool ValueWriter<Sink>::visit_tuple(const T&, Body&& body) {
    Array arr;
    ArraySeqWriter sw{arr};
    KOTA_CODEC_TRY(body(sw));
    return sink.emit(std::move(arr));
}

template <typename F>
bool TableWriter::visit_field(std::size_t /*index*/, std::string_view name, F&& writer) {
    TableValueWriter vw{
        {tbl, std::string(name)}
    };
    return writer(vw);
}

template <typename F>
bool ArraySeqWriter::visit_element(F&& writer) {
    ArrayValueWriter vw{{arr}};
    return writer(vw);
}

template <typename KF, typename VF>
bool MapWriter::visit_entry(KF&& key_fn, VF&& value_fn) {
    std::string key;
    MapKeyWriter<StringKeySink, format> kw{{key}};
    KOTA_CODEC_TRY(key_fn(kw));
    TableValueWriter vw{
        {tbl, std::move(key)}
    };
    return value_fn(vw);
}

/// Encodes `value` as a toml::Table DOM (to_string renders it as text).
template <typename Config = void, typename T>
auto to_toml(const T& value) -> std::expected<Table, toml::error> {
    // Root routing follows the representation the codec dispatch resolves
    // (annotations and toml-scoped meta::repr included), not the declared
    // type: a struct whose repr is a scalar is boxed under the root key, and
    // a repr that resolves to a table shape becomes the root table.
    using resolved_t = meta::resolved_repr_t<T, format>;
    constexpr auto kind = meta::kind_of<resolved_t>();

    if constexpr(std::is_same_v<resolved_t, T> &&
                 (kind == meta::type_kind::optional || kind == meta::type_kind::pointer)) {
        if(value) {
            auto engaged = to_toml<Config>(*value);
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
        using Cfg = default_config<Config>;
        rich_error err;
        scoped_context<rich_error> guard(err);
        Table root;

        bool ok;
        // Table-shaped values become the root table itself — including a raw
        // toml::Table, whose serialize_visit emits it verbatim (its range
        // kind would otherwise box it while the decode side reads the root).
        if constexpr(kind == meta::type_kind::structure || kind == meta::type_kind::map ||
                     std::same_as<resolved_t, Table>) {
            ValueWriter<RootSink> vw{{root}};
            ok = encode_value<Cfg>(vw, value);
        } else {
            TableValueWriter vw{
                {root, std::string(detail::boxed_root_key)}
            };
            ok = encode_value<Cfg>(vw, value);
        }

        if(!ok) {
            return std::unexpected(std::move(err));
        }
        return root;
    }
}

/// Encodes `value` as TOML text (to_toml rendered through toml++).
template <typename Config = void, typename T>
auto to_string(const T& value) -> std::expected<std::string, error> {
    auto table = to_toml<Config>(value);
    if(!table) {
        return std::unexpected(table.error());
    }

    std::ostringstream out;
    out << *table;
    return out.str();
}

}  // namespace kota::codec::toml
