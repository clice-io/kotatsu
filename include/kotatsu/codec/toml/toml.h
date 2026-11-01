#pragma once

#include "kotatsu/codec/macro.h"
#include "kotatsu/codec/toml/decode.h"
#include "kotatsu/codec/toml/encode.h"
#include "kotatsu/codec/toml/type.h"

namespace kotatsu::codec {

template <typename Format, typename Sink>
struct serialize_visit<toml::ValueWriter<Format, Sink>, toml::Table> {
    static bool visit(toml::ValueWriter<Format, Sink>& vis, const toml::Table& value) {
        return vis.sink.emit(value);
    }
};

template <typename Format, typename Sink>
struct serialize_visit<toml::ValueWriter<Format, Sink>, toml::Array> {
    static bool visit(toml::ValueWriter<Format, Sink>& vis, const toml::Array& value) {
        return vis.sink.emit(value);
    }
};

template <typename Format>
struct deserialize_visit<toml::ValueReader<Format>, toml::Table> {
    static bool visit(toml::ValueReader<Format>& vis, toml::Table& value) {
        if(!vis.node) {
            return scoped_context<rich_error>::fail(rich_error::invalid_type("table", "null"));
        }
        const auto* tbl = vis.node->as_table();
        if(!tbl) {
            return scoped_context<rich_error>::fail(
                rich_error::invalid_type("table", toml::detail::node_type_name(vis.node)));
        }
        value = *tbl;
        return true;
    }
};

template <typename Format>
struct deserialize_visit<toml::ValueReader<Format>, toml::Array> {
    static bool visit(toml::ValueReader<Format>& vis, toml::Array& value) {
        if(!vis.node) {
            return scoped_context<rich_error>::fail(rich_error::invalid_type("array", "null"));
        }
        const auto* arr = vis.node->as_array();
        if(!arr) {
            return scoped_context<rich_error>::fail(
                rich_error::invalid_type("array", toml::detail::node_type_name(vis.node)));
        }
        value = *arr;
        return true;
    }
};

}  // namespace kotatsu::codec
