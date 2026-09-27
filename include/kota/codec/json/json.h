#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "kota/codec/json/decode.h"
#include "kota/codec/json/encode.h"
#include "kota/codec/json/type.h"
#include "kota/codec/macro.h"
#include "kota/codec/visit/common.h"

namespace kota::codec::json {

/// Reformats JSON text with indentation; the input is parsed (and thus
/// validated) but not decoded into any type.
inline std::expected<std::string, error> prettify(std::string_view json) {
    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    auto padded = simdjson::padded_string(json);
    if(auto err = parser.parse(padded).get(doc)) {
        return std::unexpected(detail::simdjson_error(err));
    }
    return simdjson::prettify(doc);
}

}  // namespace kota::codec::json

namespace kota::codec {

template <typename Config>
struct serialize_visit<json::ValueWriter, RawValue, Config> {
    static bool visit(json::ValueWriter& vis, const RawValue& value) {
        if(value.empty()) {
            return vis.visit_null();
        }
        vis.builder.append_raw(value.data);
        return true;
    }
};

template <typename Config>
struct deserialize_visit<json::Reader, RawValue, Config> {
    static bool visit(json::Reader& vis, RawValue& value) {
        simdjson::ondemand::json_type t;
        if(vis.apply([&](auto& s) { return s.type().get(t); }) != simdjson::SUCCESS)
            return scoped_context<rich_error>::fail(rich_error("failed to read JSON value type"));
        std::string_view raw;
        if(vis.apply([&](auto& s) { return s.raw_json().get(raw); }) != simdjson::SUCCESS)
            return scoped_context<rich_error>::fail(rich_error("failed to read raw JSON value"));
        value.data.assign(raw.data(), raw.size());
        return true;
    }
};

}  // namespace kota::codec
