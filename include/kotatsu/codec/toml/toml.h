#pragma once

#include "kotatsu/codec/macro.h"
#include "kotatsu/codec/toml/decode.h"
#include "kotatsu/codec/toml/encode.h"
#include "kotatsu/codec/toml/type.h"

namespace kotatsu::codec {

template <typename Sink, typename Config>
struct serialize_visit<toml::ValueWriter<Sink>, toml::Table, Config> {
    static bool visit(toml::ValueWriter<Sink>& vis, const toml::Table& value) {
        return vis.sink.emit(value);
    }
};

template <typename Sink, typename Config>
struct serialize_visit<toml::ValueWriter<Sink>, toml::Array, Config> {
    static bool visit(toml::ValueWriter<Sink>& vis, const toml::Array& value) {
        return vis.sink.emit(value);
    }
};

template <typename Config>
struct deserialize_visit<toml::ValueReader, toml::Table, Config> {
    static bool visit(toml::ValueReader& vis, toml::Table& value) {
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

template <typename Config>
struct deserialize_visit<toml::ValueReader, toml::Array, Config> {
    static bool visit(toml::ValueReader& vis, toml::Array& value) {
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
