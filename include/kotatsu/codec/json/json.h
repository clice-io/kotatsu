#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "kotatsu/codec/json/decode.h"
#include "kotatsu/codec/json/encode.h"
#include "kotatsu/codec/json/type.h"
#include "kotatsu/codec/macro.h"
#include "kotatsu/codec/visit/common.h"

namespace kotatsu::codec::json {

/// Reformats JSON text with indentation; the input is parsed (and thus
/// validated) but not decoded into any type.
inline std::expected<std::string, rich_error> prettify(std::string_view json) {
    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    auto padded = simdjson::padded_string(json);
    if(auto err = parser.parse(padded).get(doc)) {
        return std::unexpected(detail::simdjson_error(err));
    }
    return simdjson::prettify(doc);
}

}  // namespace kotatsu::codec::json

namespace kotatsu::codec {

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
        // Raw JSON is often parsed again later; the room past it lets that
        // parse write simdjson's padding without moving the text.
        value.data.reserve(raw.size() + simdjson::SIMDJSON_PADDING);
        value.data.assign(raw.data(), raw.size());
        return true;
    }
};

}  // namespace kotatsu::codec
